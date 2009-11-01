/*
 * Copyright (2009) John Roark <john.roark@gmail.com>
 *
 * Usage: RokuFinder [ip to start search from]
 */

/* link with iphlpapi.lib wsock32.lib */

#include <stdio.h>

#ifdef	_WIN32
#include <Windows.h>
#include <iprtrmib.h>
#include <Iphlpapi.h>

#define	close	closesocket
#define	usleep(x)	Sleep(x/1000)
#else	// OSX
#include <sys/param.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <nlist.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#include <ifaddrs.h>

#define ROUNDUP(a) \
((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

#define	SOCKET	int
#endif

#define	ROKU_PORT	8080
#define	ROKU_HWADDR	"\x00\x0D\x4B"
#define	TIVO_HWADDR	"\x00\x11\xD9"

unsigned char * search (unsigned long ip);

void usage ()
{
	printf (
		"Usage:\n"
		"         RokuFinder <IP address to start from>\n"
		"         RokuFinder -a           (Automatically search all of the local subnet)\n"
		"         RokuFinder -h           (Display this help)\n"
		"Example:\n"
		"         RokuFinder 192.168.1.1 (Will search from 192.168.1.1 to 192.168.1.255)\n");
}

int
is_roku(unsigned char *hwAddr, unsigned long ipAddr)
{
	int rtn = 0;
		
	if (memcmp (ROKU_HWADDR, hwAddr, 3) == 0) {
		SOCKET	s = -1;
		struct sockaddr_in	saddr;
		
		saddr.sin_family = AF_INET;
		saddr.sin_port = htons (ROKU_PORT);
		saddr.sin_addr.s_addr = ipAddr;
		
		s = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
		
		if (s > 0) {
			if (connect (s, (struct sockaddr *)&saddr, sizeof (saddr)) >= 0) {
				struct in_addr addr;

				addr.s_addr	= ipAddr;
				printf("%s may be a Roku DVP\n", inet_ntoa(addr));
				rtn = 1;
			}
			
			shutdown (s, 2);
			close (s);
		}
	}
	
	return rtn;
}

int find_roku (unsigned long ip)
{
	struct sockaddr_in	saddr	= {0};
	SOCKET				s		= 0;
	int					i		= 0;
	struct in_addr		addr	= {0};
	unsigned char		*hwAddr	= NULL;
	
	addr.s_addr = ip;
	memcpy (&saddr.sin_addr, &addr, sizeof(saddr.sin_addr));
	
	i	= (saddr.sin_addr.s_addr & 0xFF000000) >> 24;
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(9);
	
	for (; i <= 255; i++) {
		hwAddr = search (saddr.sin_addr.s_addr);
		if (!hwAddr) {
			/* add this ip to the arp table and search again */
			s = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			
			if (s > 0) {
				sendto (s, "roku?\n", 6, 0, (const struct sockaddr *)&saddr, sizeof(saddr));
				shutdown (s, 2);
				close (s);
			}
			
			usleep (25000);
			hwAddr = search (saddr.sin_addr.s_addr);
		}

		if (hwAddr)
			is_roku (hwAddr, saddr.sin_addr.s_addr);

		saddr.sin_addr.s_addr += 0x01000000;
	}
	
	return 0;
}

#ifdef _WIN32
unsigned char * search (unsigned long ip)
{
	unsigned long nSize = 400;	// should be plenty?
	unsigned long dwRet	= 0;
	unsigned int i = 0;
	PMIB_IPNETTABLE pMib = NULL;
	static unsigned char	hwAddr[8] = {0};

	memset (hwAddr, 0, sizeof (hwAddr));

	pMib = (PMIB_IPNETTABLE)malloc (sizeof (MIB_IPNETTABLE) + sizeof (MIB_IPNETROW) * nSize);

	if (pMib)
	{
		memset (pMib, 0, sizeof (MIB_IPNETTABLE) + sizeof (MIB_IPNETROW) * nSize);
		dwRet = GetIpNetTable (pMib, &nSize, TRUE);     

		nSize = (unsigned long)pMib->dwNumEntries ;
		//nSize = 400;

		for (i = 0; i < nSize; i++) 
		{
			if (pMib->table[i].dwAddr == ip)
			{
				memcpy (hwAddr, pMib->table[i].bPhysAddr, sizeof (hwAddr));
				free (pMib);
				return hwAddr;
			}
		}
		free (pMib);
	}

	return NULL;
}
#else	// OSX
unsigned char * search (unsigned long ip)
{
	int mib[6];
	size_t needed;
	char *lim, *buf, *next;
	struct rt_msghdr *rtm;
	struct sockaddr_inarp *sin2;
	struct sockaddr_dl *sdl;
	static unsigned char	hwAddr[8] = {0};
	
	memset (hwAddr, 0, sizeof (hwAddr));
	
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET;
	mib[4] = NET_RT_FLAGS;
	mib[5] = RTF_LLINFO;
	if (sysctl(mib, 6, NULL, &needed, NULL, 0) >= 0)
	{
		if ((buf = malloc(needed)) != NULL)
		{

			if (sysctl(mib, 6, buf, &needed, NULL, 0) >= 0)
			{
				lim = buf + needed;
				for (next = buf; next < lim; next += rtm->rtm_msglen) {
					rtm = (struct rt_msghdr *)next;
					sin2 = (struct sockaddr_inarp *)(rtm + 1);
					sdl = (struct sockaddr_dl*)((char*)sin2 + ROUNDUP(sin2->sin_len));
					if (ip) {
						if (ip == sin2->sin_addr.s_addr)
						{
							memcpy (hwAddr, (unsigned char *)LLADDR(sdl), sizeof (hwAddr));
							free(buf);
							return hwAddr;
						}
					}
				}
				free(buf);
			}
		}
	}
	
	return NULL;
}

#endif

#ifdef _WIN32
unsigned long get_ip ()
{
	char szHostName[255];
	struct hostent *host_entry;

	gethostname(szHostName, 255);
	host_entry=gethostbyname(szHostName);

	return ((struct in_addr *)*(host_entry->h_addr_list))->s_addr;
}
#else
unsigned long get_ip ()
{
	struct ifaddrs		*addrs;
	struct ifaddrs		*addr;
	unsigned long		ret	= 0;
	
	if (getifaddrs (&addrs) != -1) {
		addr = addrs;
		while (addr != NULL)
		{
			if((addr->ifa_flags & IFF_UP) && (addr->ifa_addr->sa_family == AF_INET) && strncmp("lo0", addr->ifa_name, 3))
			{
				ret = ((struct sockaddr_in *)addr->ifa_addr)->sin_addr.s_addr;
				break;
			}
			addr = addr->ifa_next;
		}
		freeifaddrs (addrs);
	}
	
	return ret;
}
#endif

int main (int argc, char *argv[])
{
	unsigned long	ip	= 0;

	if (argc != 2)
	{
		usage ();
		return -1;
	}

#ifdef	_WIN32
	{
	WSADATA	wsaData;
	WORD	wVersion	= MAKEWORD(2,0);

	if (WSAStartup (wVersion, &wsaData))
		printf ("WSAStartup failed!\n");
	}
#endif

	if (strncmp ("-a", argv[1], 2) == 0)
	{
		struct in_addr	ipAddr;
		ip = ipAddr.s_addr = ((get_ip () & 0x00FFFFFF) + 0x01000000);
		printf("Starting at IP Address: %s\n", inet_ntoa(ipAddr));
	}
	else
	{
		ip = inet_addr (argv[1]);
	}

	if (ip != 0)
	{
		find_roku (ip);
	}

#ifdef	_WIN32
	WSACleanup ();
#endif

	return 0;
}
