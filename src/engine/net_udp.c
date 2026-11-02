// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:
//      UDP network module for LAN and internet multiplayer.
//      Uses Winsock on Windows and BSD sockets everywhere else.
//
//-----------------------------------------------------------------------------

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_stdinc.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
typedef SOCKET socket_t;
#define SOCKET_INVALID INVALID_SOCKET
#define CloseSocket closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define SOCKET_INVALID (-1)
#define CloseSocket close
#endif

#include "net_udp.h"
#include "i_system.h"
#include "m_misc.h"
#include "net_io.h"
#include "net_packet.h"

// Largest datagram we accept; matches the usual Ethernet MTU like
// Chocolate Doom's SDL_net module.

#define MAX_PACKET_SIZE 1500

// The netcode identifies peers by comparing net_addr_t pointers, so
// every remote endpoint must map to exactly one address object.

typedef struct
{
	net_addr_t net_addr;
	struct sockaddr_in sin;
} udp_addr_t;

static socket_t udp_socket = SOCKET_INVALID;
static int udp_bound_port;
static int udp_port = NET_UDP_DEFAULT_PORT;

static udp_addr_t** addr_table;
static int addr_table_size;
static int addr_table_alloced;

//
// SockAddrEqual
//

static boolean SockAddrEqual(const struct sockaddr_in* a, const struct sockaddr_in* b)
{
	return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

//
// FindAddress
// Returns the unique address object for an endpoint, creating it on first use
//

static net_addr_t* FindAddress(const struct sockaddr_in* sin)
{
	udp_addr_t* entry;
	int i;

	for (i = 0; i < addr_table_size; ++i)
	{
		if (SockAddrEqual(&addr_table[i]->sin, sin))
		{
			return &addr_table[i]->net_addr;
		}
	}

	if (addr_table_size == addr_table_alloced)
	{
		addr_table_alloced = addr_table_alloced ? addr_table_alloced * 2 : 16;
		addr_table = realloc(addr_table, sizeof(*addr_table) * addr_table_alloced);

		if (addr_table == NULL)
		{
			I_Error("NET_UDP: Out of memory for address table");
		}
	}

	entry = malloc(sizeof(*entry));

	if (entry == NULL)
	{
		I_Error("NET_UDP: Out of memory for address");
	}

	entry->net_addr.module = &net_udp_module;
	entry->net_addr.handle = entry;
	entry->sin = *sin;

	addr_table[addr_table_size++] = entry;

	return &entry->net_addr;
}

//
// GetPortParm
// Reads the -port parameter, falling back to the default port
//

static int GetPortParm(void)
{
	int p = M_CheckParm("-port");

	if (p > 0 && p < myargc - 1)
	{
		int port = SDL_atoi(myargv[p + 1]);

		if (port > 0 && port < 65536)
		{
			return port;
		}

		I_Printf("NET_UDP: Ignoring invalid port '%s'\n", myargv[p + 1]);
	}

	return NET_UDP_DEFAULT_PORT;
}

//
// OpenSocket
// Creates the non-blocking, broadcast-capable socket bound to the given port.
// Port 0 binds an ephemeral port. Opening again reuses the existing socket,
// which only works if it already listens where a server needs it to.
//

static boolean OpenSocket(int port)
{
	struct sockaddr_in sin;
	int enable = 1;

	if (udp_socket != SOCKET_INVALID)
	{
		return port == 0 || port == udp_bound_port;
	}

#ifdef _WIN32
	{
		static boolean wsa_started = false;
		WSADATA wsa_data;

		if (!wsa_started)
		{
			if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
			{
				I_Printf("NET_UDP: WSAStartup failed\n");
				return false;
			}

			wsa_started = true;
		}
	}
#endif

	udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (udp_socket == SOCKET_INVALID)
	{
		I_Printf("NET_UDP: Unable to create socket\n");
		return false;
	}

	setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, (const char*)&enable, sizeof(enable));

	SDL_zero(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons((unsigned short)port);

	if (bind(udp_socket, (struct sockaddr*)&sin, sizeof(sin)) != 0)
	{
		I_Printf("NET_UDP: Unable to bind to port %i\n", port);
		CloseSocket(udp_socket);
		udp_socket = SOCKET_INVALID;
		return false;
	}

	udp_bound_port = port;

#ifdef _WIN32
	{
		u_long nonblocking = 1;
		BOOL report_reset = FALSE;
		DWORD bytes;

		ioctlsocket(udp_socket, FIONBIO, &nonblocking);

		// Without this, an ICMP "port unreachable" from a peer that just quit
		// makes the next recvfrom fail with WSAECONNRESET.

		WSAIoctl(udp_socket, SIO_UDP_CONNRESET, &report_reset, sizeof(report_reset),
			NULL, 0, &bytes, NULL, NULL);
	}
#else
	fcntl(udp_socket, F_SETFL, fcntl(udp_socket, F_GETFL, 0) | O_NONBLOCK);
#endif

	return true;
}

//
// NET_UDP_InitClient
// Clients bind to an ephemeral port so several can run on one machine
//

static boolean NET_UDP_InitClient(void)
{
	udp_port = GetPortParm();

	return OpenSocket(0);
}

//
// NET_UDP_InitServer
//

static boolean NET_UDP_InitServer(void)
{
	udp_port = GetPortParm();

	if (!OpenSocket(udp_port))
	{
		return false;
	}

	I_Printf("NET_UDP: Listening on port %i\n", udp_port);

	return true;
}

//
// NET_UDP_SendPacket
//

static void NET_UDP_SendPacket(net_addr_t* addr, net_packet_t* packet)
{
	struct sockaddr_in broadcast;
	const struct sockaddr_in* sin;

	if (addr == &net_broadcast_addr)
	{
		SDL_zero(broadcast);
		broadcast.sin_family = AF_INET;
		broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		broadcast.sin_port = htons((unsigned short)udp_port);
		sin = &broadcast;
	}
	else
	{
		sin = &((udp_addr_t*)addr->handle)->sin;
	}

	sendto(udp_socket, (const char*)packet->data, packet->len, 0,
		(const struct sockaddr*)sin, sizeof(*sin));
}

//
// NET_UDP_RecvPacket
//

static boolean NET_UDP_RecvPacket(net_addr_t** addr, net_packet_t** packet)
{
	static byte buffer[MAX_PACKET_SIZE];
	struct sockaddr_in sin;
	socklen_t sin_len = sizeof(sin);
	int len;

	len = recvfrom(udp_socket, (char*)buffer, sizeof(buffer), 0,
		(struct sockaddr*)&sin, &sin_len);

	// Nothing waiting, or a transient error; either way there is no packet

	if (len <= 0)
	{
		return false;
	}

	*packet = NET_NewPacket(len);
	memcpy((*packet)->data, buffer, len);
	(*packet)->len = len;

	*addr = FindAddress(&sin);

	return true;
}

//
// NET_UDP_AddrToString
//

static void NET_UDP_AddrToString(net_addr_t* addr, char* buffer, int buffer_len)
{
	const struct sockaddr_in* sin = &((udp_addr_t*)addr->handle)->sin;
	char ip[INET_ADDRSTRLEN];

	inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
	SDL_snprintf(buffer, buffer_len, "%s:%i", ip, ntohs(sin->sin_port));
}

//
// NET_UDP_FreeAddress
//

static void NET_UDP_FreeAddress(net_addr_t* addr)
{
	int i;

	for (i = 0; i < addr_table_size; ++i)
	{
		if (&addr_table[i]->net_addr == addr)
		{
			free(addr_table[i]);
			addr_table[i] = addr_table[--addr_table_size];
			return;
		}
	}
}

//
// NET_UDP_ResolveAddress
// Accepts "host" or "host:port"
//

static net_addr_t* NET_UDP_ResolveAddress(char* address)
{
	struct addrinfo hints;
	struct addrinfo* result;
	struct sockaddr_in sin;
	char host[256];
	char* colon;
	int port = udp_port;

	if (address == NULL)
	{
		return NULL;
	}

	SDL_strlcpy(host, address, sizeof(host));
	colon = SDL_strrchr(host, ':');

	if (colon != NULL)
	{
		*colon = '\0';
		port = SDL_atoi(colon + 1);

		if (port <= 0 || port >= 65536)
		{
			return NULL;
		}
	}

	SDL_zero(hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(host, NULL, &hints, &result) != 0)
	{
		return NULL;
	}

	sin = *(struct sockaddr_in*)result->ai_addr;
	sin.sin_port = htons((unsigned short)port);
	freeaddrinfo(result);

	return FindAddress(&sin);
}

net_module_t net_udp_module =
{
	NET_UDP_InitClient,
	NET_UDP_InitServer,
	NET_UDP_SendPacket,
	NET_UDP_RecvPacket,
	NET_UDP_AddrToString,
	NET_UDP_FreeAddress,
	NET_UDP_ResolveAddress,
};
