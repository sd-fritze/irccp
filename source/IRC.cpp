/*
	cpIRC - C++ class based IRC protocol wrapper
	Copyright (C) 2003 Iain Sheppard
                      2012 gruetzkopf 

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

	Contacting the author:
	~~~~~~~~~~~~~~~~~~~~~~

	email:	iainsheppard@yahoo.co.uk
	IRC:	#magpie @ irc.quakenet.org

	This version of cpIRC has undergone modifications. It may have bugs not in the normal release of the library.
	Please ask the author of these modifications first if you believe that this is the case.
	
	contact gruetzkopf on #feos on irc.blitzed.org for help.
*/

#include "IRC.h"
#include <feos.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dswifi9.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#define SOCKET_ERROR -1
#define INVALID_SOCKET -1



FEOS_EXPORT IRC::IRC()
{
	hooks=0;
	chan_users=0;
	connected=false;
	sentnick=false;
	sentpass=false;
	sentuser=false;
	cur_nick=0;
}

FEOS_EXPORT IRC::~IRC()
{
	if (hooks)
		delete_irc_command_hook(hooks);
}

FEOS_EXPORT void IRC::insert_irc_command_hook(irc_command_hook* hook, char* cmd_name, int (*function_ptr)(char*, irc_reply_data*, void*))
{
	if (hook->function)
	{
		if (!hook->next)
		{
			hook->next=new irc_command_hook;
			hook->next->function=0;
			hook->next->irc_command=0;
			hook->next->next=0;
		}
		insert_irc_command_hook(hook->next, cmd_name, function_ptr);
	}
	else
	{
		hook->function=function_ptr;
		hook->irc_command=new char[strlen(cmd_name)+1];
		strcpy(hook->irc_command, cmd_name);
	}
}

FEOS_EXPORT void IRC::hook_irc_command(char* cmd_name, int (*function_ptr)(char*, irc_reply_data*, void*))
{
	if (!hooks)
	{
		hooks=new irc_command_hook;
		hooks->function=0;
		hooks->irc_command=0;
		hooks->next=0;
		insert_irc_command_hook(hooks, cmd_name, function_ptr);
	}
	else
	{
		insert_irc_command_hook(hooks, cmd_name, function_ptr);
	}
}

FEOS_EXPORT void IRC::delete_irc_command_hook(irc_command_hook* cmd_hook)
{
	if (cmd_hook->next)
		delete_irc_command_hook(cmd_hook->next);
	if (cmd_hook->irc_command)
		delete cmd_hook->irc_command;
	delete cmd_hook;
}

FEOS_EXPORT int IRC::start(char* server, int port, char* nick, char* user, char* name, char* pass)
{
	hostent* resolv;
	sockaddr_in rem;

	if (connected)
		return 1;

	irc_socket=socket(AF_INET, SOCK_STREAM, 0);
	if (irc_socket==INVALID_SOCKET)
	{
		return 1;
	}
	resolv=gethostbyname(server);
	if (!resolv)
	{
		closesocket(irc_socket);
		return 1;
	}
	memcpy(&rem.sin_addr, resolv->h_addr_list[0], 4);
	rem.sin_family=AF_INET;
	rem.sin_port=htons(port);

	if (connect(irc_socket, (const sockaddr*)&rem, sizeof(rem))==SOCKET_ERROR)
	{
		closesocket(irc_socket);
		return 1;
	}

	connected=true;
	
	cur_nick=new char[strlen(nick)+1];
	strcpy(cur_nick, nick);
        
	
       	sprintf(sockbuffer, "PASS %s\r\n", pass);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);

	sprintf(sockbuffer, "NICK %s\r\n", nick);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);

	sprintf(sockbuffer, "USER %s * 0 :%s\r\n", user, name);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);

	return 0;
}

FEOS_EXPORT void IRC::disconnect()
{
	if (connected)
	{
		connected=false;
		quit("Leaving");

		closesocket(irc_socket);
	}
}

FEOS_EXPORT int IRC::quit(char* quit_message)
{
	if (connected)
	{
		if (quit_message)
		{
			sprintf(sockbuffer, "QUIT %s\r\n", quit_message);
			send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
		}
		else
		{
			strcpy(sockbuffer, "QUIT \r\n" );
			send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
		}
	}
	return 0;
}

FEOS_EXPORT int IRC::message_loop()
{
	char buffer[1024];
	int ret_len;

	if (!connected)
	{
		return 1;
	}

	while (1)
	{
		ret_len=recv(irc_socket, buffer, 1023, 0);
		if (ret_len==SOCKET_ERROR || !ret_len)
		{
			return 1;
		}
		buffer[ret_len]='\0';
		split_to_replies(buffer);
	}

	return 0;
}

FEOS_EXPORT void IRC::split_to_replies(char* data)
{
	char* p;

	while (p=strstr(data, "\r\n"))
	{
		*p='\0';
		parse_irc_reply(data);
		data=p+2;
	}
}

FEOS_EXPORT int IRC::is_op(char* channel, char* nick)
{
	channel_user* cup;

	cup=chan_users;
	
	while (cup)
	{
		if (!strcmp(cup->channel, channel) && !strcmp(cup->nick, nick))
		{
			return cup->flags&IRC_USER_OP;
		}
		cup=cup->next;
	}

	return 0;
}

FEOS_EXPORT int IRC::is_voice(char* channel, char* nick)
{
	channel_user* cup;

	cup=chan_users;
	
	while (cup)
	{
		if (!strcmp(cup->channel, channel) && !strcmp(cup->nick, nick))
		{
			return cup->flags&IRC_USER_VOICE;
		}
		cup=cup->next;
	}

	return 0;
}

FEOS_EXPORT void IRC::parse_irc_reply(char* data)
{
	char* hostd;
	char* cmd;
	char* params;
	char buffer[514];
	irc_reply_data hostd_tmp;
	channel_user* cup;
	char* p;
	char* chan_temp;

	hostd_tmp.target=0;

	if (data[0]==':')
	{
		hostd=&data[1];
		cmd=strchr(hostd, ' ');
		if (!cmd)
			return;
		*cmd='\0';
		cmd++;
		params=strchr(cmd, ' ');
		if (params)
		{
			*params='\0';
			params++;
		}
		hostd_tmp.nick=hostd;
		hostd_tmp.ident=strchr(hostd, '!');
		if (hostd_tmp.ident)
		{
			*hostd_tmp.ident='\0';
			hostd_tmp.ident++;
			hostd_tmp.host=strchr(hostd_tmp.ident, '@');
			if (hostd_tmp.host)
			{
				*hostd_tmp.host='\0';
				hostd_tmp.host++;
			}
		}

		if (!strcmp(cmd, "JOIN"))
		{
			cup=chan_users;
			if (cup)
			{
				while (cup->nick)
				{
					if (!cup->next)
					{
						cup->next=new channel_user;
						cup->next->channel=0;
						cup->next->flags=0;
						cup->next->next=0;
						cup->next->nick=0;
					}
					cup=cup->next;
				}
				cup->channel=new char[strlen(params)+1];
				strcpy(cup->channel, params);
				cup->nick=new char[strlen(hostd_tmp.nick)+1];
				strcpy(cup->nick, hostd_tmp.nick);
			}
		}
		else if (!strcmp(cmd, "PART"))
		{
			channel_user* d;
			channel_user* prev;

			d=0;
			prev=0;
			cup=chan_users;
			while (cup)
			{
				if (!strcmp(cup->channel, params) && !strcmp(cup->nick, hostd_tmp.nick))
				{
					d=cup;
					break;
				}
				else
				{
					prev=cup;
				}
				cup=cup->next;
			}
			if (d)
			{
				if (d==chan_users)
				{
					chan_users=d->next;
					if (d->channel)
						delete [] d->channel;
					if (d->nick)
						delete [] d->nick;
					delete d;
				}
				else
				{
					if (prev)
					{
						prev->next=d->next;
					}
					chan_users=d->next;
					if (d->channel)
						delete [] d->channel;
					if (d->nick)
						delete [] d->nick;
					delete d;
				}
			}
		}
		else if (!strcmp(cmd, "QUIT"))
		{
			channel_user* d;
			channel_user* prev;

			d=0;
			prev=0;
			cup=chan_users;
			while (cup)
			{
				if (!strcmp(cup->nick, hostd_tmp.nick))
				{
					d=cup;
					if (d==chan_users)
					{
						chan_users=d->next;
						if (d->channel)
							delete [] d->channel;
						if (d->nick)
							delete [] d->nick;
						delete d;
					}
					else
					{
						if (prev)
						{
							prev->next=d->next;
						}
						if (d->channel)
							delete [] d->channel;
						if (d->nick)
							delete [] d->nick;
						delete d;
					}
					break;
				}
				else
				{
					prev=cup;
				}
				cup=cup->next;
			}
		}
		else if (!strcmp(cmd, "MODE"))
		{
			char* chan;
			char* changevars;
			channel_user* cup;
			channel_user* d;
			char* tmp;
			int i;
			bool plus;

			chan=params;
			params=strchr(chan, ' ');
			*params='\0';
			params++;
			changevars=params;

			params=strchr(changevars, ' ');
			if (!params)
			{
				return;
			}
			if (chan[0]!='#')
			{
				return;
			}
			*params='\0';
			params++;
		
			plus=false;
			for (i=0; i<(signed)strlen(changevars); i++)
			{
				switch (changevars[i])
				{
				case '+':
					plus=true;
					break;
				case '-':
					plus=false;
					break;
				case 'o':
					tmp=strchr(params, ' ');
					if (tmp)
					{
						*tmp='\0';
						tmp++;
					}
					tmp=params;
					if (plus)
					{
						// user has been opped (chan, params)
						cup=chan_users;
						d=0;
						while (cup)
						{
							if (cup->next && cup->channel)
							{
								if (!strcmp(cup->channel, chan) && !strcmp(cup->nick, tmp))
								{
									d=cup;
									break;
								}
							}
							cup=cup->next;
						}
						if (d)
						{
							d->flags=d->flags|IRC_USER_OP;
						}
					}
					else
					{
						// user has been deopped (chan, params)
						cup=chan_users;
						d=0;
						while (cup)
						{
							if (!strcmp(cup->channel, chan) && !strcmp(cup->nick, tmp))
							{
								d=cup;
								break;
							}
							cup=cup->next;
						}
						if (d)
						{
							d->flags=d->flags^IRC_USER_OP;
						}
					}
					params=tmp;
					break;
				case 'v':
					tmp=strchr(params, ' ');
					if (tmp)
					{
						*tmp='\0';
						tmp++;
					}
					if (plus)
					{
						// user has been voiced
						cup=chan_users;
						d=0;
						while (cup)
						{
							if (!strcmp(cup->channel, params) && !strcmp(cup->nick, hostd_tmp.nick))
							{
								d=cup;
								break;
							}
							cup=cup->next;
						}
						if (d)
						{
							d->flags=d->flags|IRC_USER_VOICE;
						}
					}
					else
					{
						// user has been devoiced
						cup=chan_users;
						d=0;
						while (cup)
						{
							if (!strcmp(cup->channel, params) && !strcmp(cup->nick, hostd_tmp.nick))
							{
								d=cup;
								break;
							}
							cup=cup->next;
						}
						if (d)
						{
							d->flags=d->flags^IRC_USER_VOICE;
						}
					}
					params=tmp;
					break;
				default:
					return;
					break;
				}
				// ------------ END OF MODE ---------------
			}
		}
		else if (!strcmp(cmd, "353"))
		{
			// receiving channel names list
			if (!chan_users)
			{
				chan_users=new channel_user;
				chan_users->next=0;
				chan_users->nick=0;
				chan_users->flags=0;
				chan_users->channel=0;
			}
			cup=chan_users;
			chan_temp=strchr(params, '#');
			if (chan_temp)
			{
				p=strstr(chan_temp, " :");
				if (p)
				{
					*p='\0';
					p+=2;
					while (strchr(p, ' '))
					{
						char* tmp;

						tmp=strchr(p, ' ');
						*tmp='\0';
						tmp++;
						while (cup->nick)
						{
							if (!cup->next)
							{
								cup->next=new channel_user;
								cup->next->channel=0;
								cup->next->flags=0;
								cup->next->next=0;
								cup->next->nick=0;
							}
							cup=cup->next;
						}
						if (p[0]=='@')
						{
							cup->flags=cup->flags|IRC_USER_OP;
							p++;
						}
						else if (p[0]=='+')
						{
							cup->flags=cup->flags|IRC_USER_VOICE;
							p++;
						}
						cup->nick=new char[strlen(p)+1];
						strcpy(cup->nick, p);
						cup->channel=new char[strlen(chan_temp)+1];
						strcpy(cup->channel, chan_temp);
						p=tmp;
					}
					while (cup->nick)
					{
						if (!cup->next)
						{
							cup->next=new channel_user;
							cup->next->channel=0;
							cup->next->flags=0;
							cup->next->next=0;
							cup->next->nick=0;
						}
						cup=cup->next;
					}
					if (p[0]=='@')
					{
						cup->flags=cup->flags|IRC_USER_OP;
						p++;
					}
					else if (p[0]=='+')
					{
						cup->flags=cup->flags|IRC_USER_VOICE;
						p++;
					}
					cup->nick=new char[strlen(p)+1];
					strcpy(cup->nick, p);
					cup->channel=new char[strlen(chan_temp)+1];
					strcpy(cup->channel, chan_temp);
				}
			}
		}
		else if (!strcmp(cmd, "NOTICE"))
		{
			hostd_tmp.target=params;
			params=strchr(hostd_tmp.target, ' ');
			if (params)
				*params='\0';
			params++;
		}
		else if (!strcmp(cmd, "PRIVMSG"))
		{
			hostd_tmp.target=params;
			params=strchr(hostd_tmp.target, ' ');
			if (!params)
				return;
			*(params++)='\0';
		}
		else if (!strcmp(cmd, "NICK"))
		{
			if (!strcmp(hostd_tmp.nick, cur_nick))
			{
				delete [] cur_nick;
				cur_nick=new char[strlen(params)+1];
				strcpy(cur_nick, params);
			}
		}
		call_hook(cmd, params, &hostd_tmp);
	}
	else
	{
		cmd=data;
		data=strchr(cmd, ' ');
		if (!data)
			return;
		*data='\0';
		params=data+1;

		if (!strcmp(cmd, "PING"))
		{
			if (!params)
				return;
			sprintf(sockbuffer, "PONG %s\r\n", &params[1]);
			send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
			0;
		}
		else
		{
			hostd_tmp.host=0;
			hostd_tmp.ident=0;
			hostd_tmp.nick=0;
			hostd_tmp.target=0;
			call_hook(cmd, params, &hostd_tmp);
		}
	}
}

FEOS_EXPORT void IRC::call_hook(char* irc_command, char* params, irc_reply_data* hostd)
{
	irc_command_hook* p;

	if (!hooks)
		return;

	p=hooks;
	while (p)
	{
		if (!strcmp(p->irc_command, irc_command))
		{
			(*(p->function))(params, hostd, this);
			p=0;
		}
		else
		{
			p=p->next;
		}
	}
}

FEOS_EXPORT int IRC::notice(char* target, char* message)
{
	if (!connected)
		return 1;
	sprintf(sockbuffer, "NOTICE %s :%s\r\n", target, message);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);	
	return 0;
}

FEOS_EXPORT int IRC::notice(char* fmt, ...)
{
	va_list argp;
	char* target;
	
	if (!connected)
		return 1;
	va_start(argp, fmt);

	sprintf(sockbuffer, "NOTICE %s :", fmt);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);

	vsprintf(sockbuffer, va_arg(argp, char*), argp);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	va_end(argp);
	strcpy(sockbuffer, "\r\n");
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	return 0;
}

FEOS_EXPORT int IRC::privmsg(char* target, char* message)
{
	if (!connected)
		return 1;
	sprintf(sockbuffer, "PRIVMSG %s :%s\r\n", target, message);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	return 0;
}

FEOS_EXPORT int IRC::privmsg(char* fmt, ...)
{
	va_list argp;
	char* target;
	
	if (!connected)
		return 1;
	va_start(argp, fmt);
	sprintf(sockbuffer, "PRIVMSG %s :", fmt);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	vsprintf(sockbuffer, va_arg(argp, char*), argp);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	va_end(argp);
	strcpy(sockbuffer, "\r\n");
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	return 0;
}


FEOS_EXPORT int IRC::join(char* channel)
{
	if (!connected)
		return 1;
	sprintf(sockbuffer, "JOIN %s\r\n", channel);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	return 0;
}

FEOS_EXPORT int IRC::part(char* channel)
{
	if (!connected)
		return 1;
	sprintf(sockbuffer, "PART %s\r\n", channel);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	return 0;
}

int IRC::kick(char* channel, char* nick)
{
	if (!connected)
		return 1;
	sprintf(sockbuffer, "KICK %s %s\r\n", channel, nick);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	return 0;
}

FEOS_EXPORT int IRC::raw(char* data)
{
	if (!connected)
		return 1;
	sprintf(sockbuffer, "%s\r\n", data);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	return 0;
}

FEOS_EXPORT int IRC::kick(char* channel, char* nick, char* message)
{
	if (!connected)
		return 1;
	sprintf(sockbuffer, "KICK %s %s :%s\r\n", channel, nick, message);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	return 0;
}

FEOS_EXPORT int IRC::mode(char* channel, char* modes, char* targets)
{
	if (!connected)
		return 1;
	if (!targets)
	{
		sprintf(sockbuffer, "MODE %s %s\r\n", channel, modes);
		send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	}
	else
	{
		sprintf(sockbuffer, "MODE %s %s %s\r\n", channel, modes, targets);
		send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	}
	return 0;
}

FEOS_EXPORT int IRC::mode(char* modes)
{
	if (!connected)
		return 1;
	mode(cur_nick, modes, 0);
	return 0;
}

FEOS_EXPORT int IRC::nick(char* newnick)
{
	if (!connected)
		return 1;
	sprintf(sockbuffer, "NICK %s\r\n", newnick);
	send(irc_socket, sockbuffer, strlen(sockbuffer), 0);
	return 0;
}

FEOS_EXPORT char* IRC::current_nick()
{
	return cur_nick;
}

