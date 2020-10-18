/*
 * UnrealIRCd 5 PROXYv1 copy module
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
  "third/proxyv1_copy",
  "0.0.1",
  "Copy received packets via PROXY v1",
  "Les De Ridder <les@lesderid.net>",
  "unrealircd-5"
};

struct sockaddr_in target_addr;

ModDataInfo *proxyv1_copy_moddata_info;

int proxyv1_copy_rawpacket_in(Client *sptr, char *readbuf, int *length);
int proxyv1_copy_handshake(Client *sptr);

void proxyv1_copy_moddata_free(ModData *m);

char *getserverip(Client *client);

#define CLIENT_SOCKET(client) (moddata_client(client, proxyv1_copy_moddata_info).i)

MOD_TEST()
{
  //TODO: Test config
  return MOD_SUCCESS;
}

MOD_INIT()
{
  HookAdd(modinfo->handle, HOOKTYPE_RAWPACKET_IN, 0, proxyv1_copy_rawpacket_in);
  HookAdd(modinfo->handle, HOOKTYPE_HANDSHAKE, 0, proxyv1_copy_handshake);

  ModDataInfo moddata_info;
  memset(&moddata_info, 0, sizeof(moddata_info));
  moddata_info.name = "proxyv1_copy";
  moddata_info.serialize = NULL;
  moddata_info.unserialize = NULL;
  moddata_info.free = proxyv1_copy_moddata_free;
  moddata_info.sync = 0;
  moddata_info.type = MODDATATYPE_CLIENT;
  proxyv1_copy_moddata_info = ModDataAdd(modinfo->handle, moddata_info);

  target_addr.sin_family = AF_INET;
  target_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  target_addr.sin_port = htons(6667);

  return MOD_SUCCESS;
}

MOD_LOAD()
{
  //TODO: Send existing users (including mode, away status), channels
  return MOD_SUCCESS;
}

MOD_UNLOAD()
{
  return MOD_SUCCESS;
}

int proxyv1_copy_rawpacket_in(Client *sptr, char *readbuf, int *length)
{
  if (*length < 0)
    return 1;

  //TODO: Fix potential race with proxyv1_copy_on_connected
  ssize_t sent = send(CLIENT_SOCKET(sptr), readbuf, *length, 0);
  if (sent < 0)
  {
    fprintf(stderr, "[proxyv1_copy] send returned %ld\n", sent);
  }

  return 1; //continue parsing
}

void proxyv1_copy_on_connected(int fd, int revents, void *data)
{
  fd_setselect(fd, FD_SELECT_WRITE, NULL, NULL);

  Client *client = data;

  char proxy_header[5 + 1 + 4 + 1 + HOSTLEN + 1 + HOSTLEN + 1 + 5 + 1 + 5 + 1 + 2 + 1];
  int header_size = sprintf(proxy_header,
      "PROXY %s %s %s %d %d\r\n",
      IsIPV6(client) ? "TCP6" : "TCP4",
      client->local->sockhost,
      getserverip(client),
      client->local->port,
      client->local->listener->port);

  ssize_t sent = send(fd, proxy_header, header_size, 0);
  if (sent < 0)
  {
    fprintf(stderr, "[proxyv1_copy] send returned %ld\n", sent);
  }

  CLIENT_SOCKET(client) = fd;
}

void proxyv1_copy_connect(Client *client)
{
  int fd;
  if ((fd = fd_socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0, "proxyv1_copy")) < 0)
  {
    fprintf(stderr, "[proxyv1_copy] socket() error: %s\n", strerror(errno));
  }

  int ret = connect(fd, (struct sockaddr*)&target_addr, sizeof(target_addr));
  if (ret < 0 && errno != EINPROGRESS)
  {
    fprintf(stderr, "[proxyv1_copy] connect() error: %s\n", strerror(errno));
    return;
  }

  fd_setselect(fd, FD_SELECT_WRITE, proxyv1_copy_on_connected, client);
}

int proxyv1_copy_handshake(Client *sptr)
{
  if (!sptr->local)
    return 0;

  proxyv1_copy_connect(sptr);

  return 0; //no significance
}

void proxyv1_copy_moddata_free(ModData *m)
{
  int fd = m->i;
  fd_close(fd);
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
