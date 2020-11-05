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

#include <poll.h>

//TODO: Disable module when PROXY server is unavailable?

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
void proxyv1_copy_connect(Client *client);
void proxyv1_copy_send_initial_client(Client *client);
void proxyv1_copy_send_initial_channels();
void proxyv1_copy_send_initial_channel(Channel *channel);
void proxyv1_copy_send_initial_channel_list_mode(Channel *channel, Member *most_privileged, char type, Ban *head);
void proxyv1_copy_send(Client *client, char *buffer, size_t length);
void proxyv1_copy_send_blocking(Client *client, char *buffer, size_t length);
size_t proxyv1_copy_recv(Client *client, char *buffer, size_t length);

void proxyv1_copy_on_connected(int fd, int revents, void *data);
void proxyv1_copy_on_incoming(int fd, int revents, void *data);

//HACK: sizeof(long) is not guaranteed to be 2*sizeof(int)
#define MODDATA_SOCKET(moddata) (((int*)&(moddata).l)[0])
#define MODDATA_SENDINITIAL(moddata) (((int*)&(moddata).l)[1])
#define CLIENT_SOCKET(client) MODDATA_SOCKET(moddata_client(client, proxyv1_copy_moddata_info))
#define CLIENT_SENDINITIAL(client) MODDATA_SENDINITIAL(moddata_client(client, proxyv1_copy_moddata_info))

void proxyv1_copy_moddata_free(ModData *m);

char *getserverip(Client *client);
char *getclientip(Client *client);

int clients_to_init = -1;

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
  Client *client;
  list_for_each_entry(client, &lclient_list, lclient_node)
  {
    //fprintf(stderr, "[proxyv1_copy] nick=\"%s\", fd=%d\n", client->name, CLIENT_SOCKET(client));

    if (!CLIENT_SOCKET(client))
    {
      CLIENT_SENDINITIAL(client) = true;

      if (clients_to_init == -1)
	clients_to_init = 1;
      else
	clients_to_init++;
    }
  }

  list_for_each_entry(client, &lclient_list, lclient_node)
  {
    if (CLIENT_SENDINITIAL(client))
    {
      proxyv1_copy_connect(client);
    }
  }

  return MOD_SUCCESS;
}

MOD_UNLOAD()
{
  return MOD_SUCCESS;
}

size_t proxyv1_copy_recv(Client *client, char *buffer, size_t length)
{
  ssize_t bytes_read = recv(CLIENT_SOCKET(client), buffer, length, 0);
  if (bytes_read < 0)
  {
    //fprintf(stderr, "[proxyv1_copy] recv returned %ld (PROXY server down?)\n", bytes_read);
  }
  else if (bytes_read > 0)
  {
    //fprintf(stderr, "[proxyv1_copy] C[%s]> %.*s", client->name, (int) bytes_read, buffer);
  }

  return bytes_read;
}

void proxyv1_copy_send(Client *client, char *buffer, size_t length)
{
  //TODO: check if connected?

  //fprintf(stderr, "[proxyv1_copy] S[%s]> %.*s", client->name, (int) length, buffer);

  ssize_t sent = send(CLIENT_SOCKET(client), buffer, length, 0);
  if (sent < 0)
  {
    //fprintf(stderr, "[proxyv1_copy] send returned %ld (PROXY server down?)\n", sent);
  }
  else
  {
    fd_setselect(CLIENT_SOCKET(client), FD_SELECT_READ, proxyv1_copy_on_incoming, client); //HACK
  }
}

void proxyv1_copy_send_blocking(Client *client, char *buffer, size_t length)
{
  //HACK

  int fd = CLIENT_SOCKET(client);
  fd_setselect(fd, FD_SELECT_READ, NULL, NULL); //disable select

  struct pollfd poll_fd = { .fd = fd, .events = POLLIN };
  switch (poll(&poll_fd, 1, 100))
  {
      case -1:
	  //fprintf(stderr, "[proxyv1_copy] poll returned -1 (PROXY server down?)\n");
          return;
      case 0:
          break;
      default:
	  proxyv1_copy_on_incoming(0, 0, client);
          break;
  }

  int flags = fcntl(fd, F_GETFL);
  //TODO: assert non-blocking

  flags ^= O_NONBLOCK;
  fcntl(fd, F_SETFL, flags); //set blocking

  proxyv1_copy_send(client, buffer, length); //send
  proxyv1_copy_on_incoming(0, 0, client); //read (HACK: slow)

  flags ^= O_NONBLOCK;
  fcntl(fd, F_SETFL, flags); //set non-blocking
}

void proxyv1_copy_send_initial_channel(Channel *channel)
{
  char line[512 + 1];
  size_t length;

  //fprintf(stderr, "channel name: %s\n", channel->chname);

  //1. find most privileged member
  Member *most_privileged = NULL;
  for (Member *member = channel->members; member != NULL; member = member->next)
  {
    if (member->flags & CHFL_CHANOWNER)
    {
      most_privileged = member;
      break;
    }
    else if (most_privileged == NULL
	|| (member->flags & CHFL_HALFOP && !(most_privileged->flags & CHFL_HALFOP_OR_HIGHER))
	|| (member->flags & CHFL_CHANOP && !(most_privileged->flags & CHFL_CHANOP_OR_HIGHER))
	|| (member->flags & CHFL_CHANADMIN && !(most_privileged->flags & CHFL_CHANADMIN)))
    {
      most_privileged = member;
    }
  }

  if (most_privileged == NULL)
  {
    //fprintf(stderr, "[proxyv1_copy] channel \"%s\" is empty!\n", channel->chname);
    return;
  }
  else
  {
    //fprintf(stderr, "most_privileged = %s\n", most_privileged->client->name);
  }

  //2. join as most privileged member
  length = sprintf(line, "JOIN %s\r\n", channel->chname);
  proxyv1_copy_send_blocking(most_privileged->client, line, length);

  Member *topic_setter = NULL;
  //3. join as other members, and use most privileged member to set their modes
  for (Member *member = channel->members; member != NULL; member = member->next)
  {
    if (!topic_setter && channel->topic_nick && strcmp(channel->topic_nick, member->client->name) == 0)
    {
      topic_setter = member;
    }

    if (member != most_privileged)
    {
      length = sprintf(line, "JOIN %s\r\n", channel->chname);
      proxyv1_copy_send_blocking(member->client, line, length);
    }

    int count = 0;
    char modes[6];
    char params[256];
    modes[0] = params[0] = '\0';

    if (member->flags & CHFL_VOICE)
    {
      modes[count++] = 'v';
      sprintf(strchr(params, '\0'), " %s", member->client->name);
    }
    if (member->flags & CHFL_HALFOP)
    {
      modes[count++] = 'h';
      sprintf(strchr(params, '\0'), " %s", member->client->name);
    }
    if (member->flags & CHFL_CHANOP)
    {
      modes[count++] = 'o';
      sprintf(strchr(params, '\0'), " %s", member->client->name);
    }
    if (member->flags & CHFL_CHANADMIN)
    {
      modes[count++] = 'a';
      sprintf(strchr(params, '\0'), " %s", member->client->name);
    }
    if (member->flags & CHFL_CHANOWNER)
    {
      modes[count++] = 'q';
      sprintf(strchr(params, '\0'), " %s", member->client->name);
    }
    modes[count] = '\0';

    if (count > 0)
    {
      length = sprintf(line, "MODE %s +%s%s\r\n", channel->chname, modes, params);
      proxyv1_copy_send_blocking(most_privileged->client, line, length);
    }
  }

  //4. send topic (by original setter if they're in the channel, otherwise by most privileged)
  if (channel->topic && strlen(channel->topic) > 0)
  {
    if (topic_setter == NULL
        || ((channel->mode.mode & MODE_TOPICLIMIT) && !(topic_setter->flags & CHFL_HALFOP_OR_HIGHER)))
    {
      topic_setter = most_privileged;
    }

    length = sprintf(line, "TOPIC %s :%s\r\n", channel->chname, channel->topic);
    proxyv1_copy_send(topic_setter->client, line, length);
  }

  //5. send regular modes
  char modes_buffer[32];
  char params_buffer[256];
  channel_modes(most_privileged->client, modes_buffer, params_buffer, 32, 256, channel);
  length = sprintf(line, "MODE %s %s %s\r\n", channel->chname, modes_buffer, params_buffer);
  proxyv1_copy_send(most_privileged->client, line, length);

  //6. send list modes (+b/+e/+I)
  proxyv1_copy_send_initial_channel_list_mode(channel, most_privileged, 'b', channel->banlist);
  proxyv1_copy_send_initial_channel_list_mode(channel, most_privileged, 'e', channel->exlist);
  proxyv1_copy_send_initial_channel_list_mode(channel, most_privileged, 'I', channel->invexlist);

  //7. send invites
  //TODO
}

void proxyv1_copy_send_initial_channel_list_mode(Channel *channel, Member *most_privileged, char type, Ban *head)
{
  char line[512 + 1];
  size_t length;

  Ban *ban;
  for (ban = head; ban != NULL; ban = ban->next)
  {
    Member *banner = NULL;
    for (Member *member = channel->members; member != NULL; member = member->next)
    {
      if (!banner && strcmp(ban->who, member->client->name) == 0)
      {
        banner = member;
	break;
      }
    }
    if (!banner || !(banner->flags & CHFL_HALFOP_OR_HIGHER))
    {
      banner = most_privileged;
    }

    length = sprintf(line, "MODE %s +%c %s\r\n", channel->chname, type, ban->banstr);
    proxyv1_copy_send(banner->client, line, length);
  }
}

void proxyv1_copy_send_initial_channels()
{
  //fprintf(stderr, "[proxyv1_copy] sending initial channel data\n");

  for (Channel *channel = channels; channel != NULL; channel = channel->nextch)
  {
    proxyv1_copy_send_initial_channel(channel);
  }
}

void proxyv1_copy_send_initial_client(Client *client)
{
  fprintf(stderr, "[proxyv1_copy] sending initial data for client \"%s\"\n", client->name);

  char line[512 + 1];
  size_t length;

  if (client->name)
  {
    length = sprintf(line, "NICK %s\r\n", client->name);

    proxyv1_copy_send(client, line, length);
  }

  //TODO: Check if umodes is correct
  if (client->user && client->user->username && client->info)
  {
    length = sprintf(line, "USER %s %ld * :%s\r\n",
	client->user->username,
	client->umodes,
	client->info);
    proxyv1_copy_send(client, line, length);
  }

  if (client->user && client->user->away)
  {
    length = sprintf(line, "AWAY :%s\r\n", client->user->away);

    proxyv1_copy_send(client, line, length);
  }

  CLIENT_SENDINITIAL(client) = false;

  clients_to_init--;

  if (clients_to_init == 0)
    proxyv1_copy_send_initial_channels();
}

int proxyv1_copy_rawpacket_in(Client *client, char *readbuf, int *length)
{
  if (*length < 0)
    return 1;

  //TODO: Fix potential race with proxyv1_copy_on_connected
  proxyv1_copy_send(client, readbuf, *length);

  return 1; //continue parsing
}

void proxyv1_copy_on_connected(int fd, int revents, void *data)
{
  Client *client = data;

  int error = 0;
  socklen_t error_size = sizeof(error);
  if (!fd || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) != 0 || error != 0)
  {
    fprintf(stderr, "[proxyv1_copy] could not connect (PROXY server down?)\n");
    return;
  }

  fd_setselect(fd, FD_SELECT_WRITE, NULL, NULL);
  fd_setselect(fd, FD_SELECT_READ, proxyv1_copy_on_incoming, client);

  char proxy_header[5 + 1 + 4 + 1 + HOSTLEN + 1 + HOSTLEN + 1 + 5 + 1 + 5 + 1 + 2 + 1];
  int header_size = sprintf(proxy_header,
      "PROXY %s %s %s %d %d\r\n",
      IsIPV6(client) ? "TCP6" : "TCP4",
      getclientip(client),
      getserverip(client),
      client->local->port,
      client->local->listener->port);

  CLIENT_SOCKET(client) = fd;

  proxyv1_copy_send(client, proxy_header, header_size);

  if (CLIENT_SENDINITIAL(client))
    proxyv1_copy_send_initial_client(client);
}

void proxyv1_copy_on_incoming(int fd, int revents, void *data)
{
  Client *client = data;

  char buffer[512 * 8 + 1];
  size_t bytes_read = proxyv1_copy_recv(client, buffer, 512 * 8 + 1);
  if (bytes_read > 0)
  {
    //fprintf(stderr, "[proxyv1_copy] C[%s] bytes read: %ld\n", client->name, bytes_read);
  }
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
  int fd = MODDATA_SOCKET(*m);
  fd_close(fd);
}

//Adapted from UnrealIRCd5's getpeerip, src/socket.c (GPLv2)
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

//Adapted from UnrealIRCd5's getpeerip, src/socket.c (GPLv2)
//Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Computing Center.
char *getclientip(Client *client)
{
  static char ret[HOSTLEN+1];

  if (IsIPV6(client))
  {
    struct sockaddr_in6 addr;
    int len = sizeof(addr);

    if (getpeername(client->local->fd, (struct sockaddr *)&addr, &len) < 0)
      return NULL;
    return inetntop(AF_INET6, &addr.sin6_addr.s6_addr, ret, sizeof(ret));
  }
  else
  {
    struct sockaddr_in addr;
    int len = sizeof(addr);

    if (getpeername(client->local->fd, (struct sockaddr *)&addr, &len) < 0)
      return NULL;
    return inetntop(AF_INET, &addr.sin_addr.s_addr, ret, sizeof(ret));
  }
}
