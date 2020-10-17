/*
 * UnrealIRCd 5 PROXYv1 module
 *
 * Copyright (C) 2020 Les De Ridder <les@lesderid.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "unrealircd.h"

ModuleHeader MOD_HEADER
= {
  "third/proxyv1",
  "0.0.1",
  "Copy received packets via PROXY v1",
  "Les De Ridder <les@lesderid.net>",
  "unrealircd-5"
};

int proxyv1_rawpacket_in(Client *sptr, char *readbuf, int *length);
int proxyv1_handshake(Client *sptr);
int proxyv1_local_quit(Client *sptr, MessageTag *mtags, char *comment);
#define proxyv1_unkuser_quit proxyv1_local_quit

char *getserverip(Client *client);

MOD_TEST()
{
  /* Test here */
  return MOD_SUCCESS;
}

MOD_INIT()
{
  HookAdd(modinfo->handle, HOOKTYPE_RAWPACKET_IN, 0, proxyv1_rawpacket_in);
  HookAdd(modinfo->handle, HOOKTYPE_HANDSHAKE, 0, proxyv1_handshake);
  HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, proxyv1_local_quit);
  HookAdd(modinfo->handle, HOOKTYPE_UNKUSER_QUIT, 0, proxyv1_unkuser_quit);
  return MOD_SUCCESS;
}

MOD_LOAD()
{
  return MOD_SUCCESS;
}

MOD_UNLOAD()
{
  return MOD_SUCCESS;
}

int proxyv1_rawpacket_in(Client *sptr, char *readbuf, int *length)
{
  if (*length < 0)
  {
    printf("received 0 len\n");
  }

  printf("received packet (len=%d): %.*s\n", *length, *length, readbuf);

  return 1; //continue parsing
}

int proxyv1_handshake(Client *sptr)
{
  printf("client handshake\n");

  if (!sptr->local)
    return 0;

  char proxy_header[5 + 1 + 4 + 1 + HOSTLEN + 1 + HOSTLEN + 1 + 5 + 1 + 5 + 1 + 2 + 1];
  sprintf(proxy_header,
      "PROXY %s %s %s %d %d\r\n",
      IsIPV6(sptr) ? "TCP6" : "TCP4",
      sptr->local->sockhost,
      getserverip(sptr),
      sptr->local->port,
      sptr->local->listener->port);

  printf(proxy_header);

  return 0; //no significance
}

int proxyv1_local_quit(Client *sptr, MessageTag *mtags, char *comment)
{
  printf("client quit: %s\n", comment); //no significance

  return 0; //no significance
}

//Code adapted from UnrealIRCd5's getpeerip, src/socket.c (GPLv2)
//Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Computing Center.
char *getserverip(Client *client)
{
  static char ret[HOSTLEN+1];

  if (IsIPV6(client))
  {
    struct sockaddr_in6 addr;
    int len = sizeof(addr);

    if (getsockname(client->local->fd, (struct sockaddr *)&addr, &len) < 0)
      return NULL;
    return inetntop(AF_INET6, &addr.sin6_addr.s6_addr, ret, sizeof(ret));
  }
  else
  {
    struct sockaddr_in addr;
    int len = sizeof(addr);

    if (getsockname(client->local->fd, (struct sockaddr *)&addr, &len) < 0)
      return NULL;
    return inetntop(AF_INET, &addr.sin_addr.s_addr, ret, sizeof(ret));
  }
}
