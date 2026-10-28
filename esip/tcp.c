/*
(C) 2009+ Ekrem Karacan
*/
#include	<stdio.h>
#include	<string.h>
#include	<sys/epoll.h>
#include	<netdb.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<errno.h>
#include	<stdlib.h>
#include	<sys/socket.h>
#include	<netinet/in.h>
#include	<arpa/inet.h>
#include	<linux/netlink.h>
#include	<linux/rtnetlink.h>
#include	<sys/ioctl.h>
#include	<net/if.h>


#include	"utils.h"
#include	"tcp.h"


int     GetUDPSock(int port);


HandleEntry	*AddTCPServer6(HandleList *hl, int port, void *callback) {
	return AddTCPServerToIP(hl, "0.0.0.0", port, callback);
}

HandleEntry	*AddTCPServer(HandleList *hl, int port, void *callback) {
	return AddTCPServerToIPv6(hl, "::1", port, callback);
}

HandleEntry	*AddTCPServerToIPv6(HandleList *hl, char *addr, int port, void *callback) {

	HandleEntry		*he;
	struct	sockaddr_in6	LocalAddr;
	int			i, k, sz, pollres, idx;
	
	

	LogPush("AddTCPServer");

	he = GetHandle(hl);
	idx = he->__idx;
	if (!he) {
		//debugPrint("} AddTCPServer.no handle\n");
		LogPrint("No handle!\n");
		LogPop();
		return NULL;
	}
	bzero(((char *) he)+sizeof(llist_entry), sizeof(HandleEntry)-sizeof(llist_entry));
	he->__idx = idx;
	he->fhin = socket(AF_INET6, SOCK_STREAM, 0);
	if (he->fhin < 0) {
		LogPrint("No socket!\n");
//		LogPop();
		goto l0;
	}
	bzero(&LocalAddr, sizeof(LocalAddr));
	LocalAddr.sin6_family = AF_INET6;

	//LocalAddr.sin_addr.s_addr = inet_addr(addr);
	inet_pton(AF_INET, addr, &LocalAddr.sin6_addr);//

	// htonl(INADDR_ANY);
	LocalAddr.sin6_port = htons(port);
	i = 1;
	if (!setsockopt(he->fhin, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i))) {
		printf("Setsockopt error\n");
	}
	sz = sizeof(i);
	getsockopt(he->fhin, SOL_SOCKET, SO_REUSEADDR, &i, (socklen_t *) &sz);
	printf("REUSEADDR : %d\n", i);
	if (bind(he->fhin,
		(struct sockaddr *) &LocalAddr,
		sizeof(LocalAddr))
	) {
		LogPrint("No bind!\n");
//		LogPop();
		goto l1;
	}
	if (listen(he->fhin, 5000)) {
		LogPrint("No listen!\n");
//		LogPop();
		goto l1;
	}
	LogPrint("TCP portu (%d) acildi\n", port);
	he->CallBack = callback;
	he->inuse = 1;
	he->Timeout = 0;
	he->EventTimeout = 0;

	he->Port = port;
	//he->idx = ++hl->startidx;
	he->handletype = SERVER_HANDLE;
	if (he->CallBack) {
		int	r;
		if ((r = (*he->CallBack)(hl, he, EVENT_CREATE, NULL, 0, he->ExtraData))) {
			//CloseHandle(hl, he);
			goto l0;
		}
	}
	he->pf = NULL;
	k = he->le.ix;
	hl->polls[k].events = EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLOUT;
	hl->polls[k].data.ptr = he;
	he->pf = hl->polls+k;
	he->pollfd = hl->pollfd;
	if ((pollres = epoll_ctl(he->pollfd, EPOLL_CTL_ADD, he->fhin, he->pf)) < 0) {
//		perror("epoll_ctl");
		//CloseHandle(hl, he);
		goto l0;
	}
	LogPrint("AddTCPServer done\n");
	LogPop();
	return he;
l1:
	close(he->fhin);
	he->fhin = 0;
l0:
	CloseHandle(hl, he);
	LogPrint("Failed\n");
	LogPop();
	return NULL;
}
HandleEntry	*AddTCPServerToIP(HandleList *hl, char *addr, int port, void *callback) {

	HandleEntry		*he;
	struct	sockaddr_in	LocalAddr;
	int			i, k, sz, pollres, idx;

	LogPush("AddTCPServer");

	he = GetHandle(hl);
	idx = he->__idx;
	if (!he) {
		//debugPrint("} AddTCPServer.no handle\n");
		LogPrint("No handle!\n");
		LogPop();
		return NULL;
	}
	bzero(((char *) he)+sizeof(llist_entry), sizeof(HandleEntry)-sizeof(llist_entry));
	he->__idx = idx;
	he->fhin = socket(PF_INET, SOCK_STREAM, 0);
	if (he->fhin < 0) {
		LogPrint("No socket!\n");
//		LogPop();
		goto l0;
	}
	bzero(&LocalAddr, sizeof(LocalAddr));
	LocalAddr.sin_family = AF_INET;

	LocalAddr.sin_addr.s_addr = inet_addr(addr);
	// htonl(INADDR_ANY);
	LocalAddr.sin_port = htons(port);
	i = 1;
	if (!setsockopt(he->fhin, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i))) {
		printf("Setsockopt error\n");
	}
	sz = sizeof(i);
	getsockopt(he->fhin, SOL_SOCKET, SO_REUSEADDR, &i, (socklen_t *) &sz);
	printf("REUSEADDR : %d\n", i);
	if (bind(he->fhin,
		(struct sockaddr *) &LocalAddr,
		sizeof(LocalAddr))
	) {
		LogPrint("No bind!\n");
//		LogPop();
		goto l1;
	}
	if (listen(he->fhin, 5000)) {
		LogPrint("No listen!\n");
//		LogPop();
		goto l1;
	}
	LogPrint("TCP portu (%d) acildi\n", port);
	he->CallBack = callback;
	he->inuse = 1;
	he->Timeout = 0;
	he->EventTimeout = 0;
	//he->idx = ++hl->startidx;
	he->handletype = SERVER_HANDLE;
	if (he->CallBack) {
		int	r;
		if ((r = (*he->CallBack)(hl, he, EVENT_CREATE, NULL, 0, he->ExtraData))) {
			//CloseHandle(hl, he);
			goto l0;
		}
	}
	he->pf = NULL;
	k = he->le.ix;
	hl->polls[k].events = EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLOUT;
	hl->polls[k].data.ptr = he;
	he->pf = hl->polls+k;
	he->pollfd = hl->pollfd;
	if ((pollres = epoll_ctl(he->pollfd, EPOLL_CTL_ADD, he->fhin, he->pf)) < 0) {
//		perror("epoll_ctl");
		//CloseHandle(hl, he);
		goto l0;
	}
	LogPrint("AddTCPServer done\n");
	LogPop();
	return he;
l1:
	close(he->fhin);
	he->fhin = 0;
l0:
	CloseHandle(hl, he);
	LogPrint("Failed\n");
	LogPop();
	return NULL;
}
HandleEntry	*AcceptTCPClient(HandleList *hl, HandleEntry *hep, char *name) {
	HandleEntry		*he;
	struct	sockaddr_in6	RemoteAddr;
	int			i, k, pollres;

	LogPush("AcceptTCPClient");
	he = GetHandle(hl);
	if (!he) {
		LogPrint("No handle\n");
		LogPop();
		return NULL;
	}
	bzero(((char *) he)+sizeof(llist_entry), sizeof(HandleEntry)-sizeof(llist_entry));
	i = sizeof(RemoteAddr);
	he->fhin = accept(hep->fhin, (struct sockaddr *) &RemoteAddr, (socklen_t *) &i);
	if (he->fhin < 0) {
		LogPrint("Accept error\n");
		LogPop();
		return NULL;
	}
	char *ntop = inet_ntop(RemoteAddr.sin6_family, &RemoteAddr.sin6_addr.__in6_u.__u6_addr8[0], he->IpAddress, sizeof(he->IpAddress));
//	strcpy(he->IpAddress, (char *) inet_ntoa(RemoteAddr.sin_addr));

//1	he->rIP = RemoteAddr.sin6_addr.__in6_u;
	if (RemoteAddr.sin6_family == AF_INET)
		memcpy(&he->rIP, &RemoteAddr.sin6_addr.__in6_u.__u6_addr8[12], 4);
	else
		memcpy(&he->rIP6, &RemoteAddr.sin6_addr.__in6_u.__u6_addr8[0], 16); 
	he->rPort = RemoteAddr.sin6_port;
	fcntl(he->fhin, F_SETFL, O_NONBLOCK
	// | fcntl(he->fhin, F_GETFL, 0)
	);
	strcpy(he->name, name);
	he->CallBack = hep->CallBack;
	he->Parent = hep;
	he->inuse = 1;
	he->Timeout = 0;
	he->EventTimeout = 0;
	//he->idx = ++hl->startidx;
	he->handletype = CLIENT_HANDLE;
	he->fhout = he->fhin;

	k = he->le.ix;
	hl->polls[k].events = EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLOUT;
	hl->polls[k].data.ptr = he;
	he->pf = hl->polls+k;
	he->pollfd = hl->pollfd;
	if ((pollres = epoll_ctl(he->pollfd, EPOLL_CTL_ADD, he->fhin, he->pf)) < 0) {
//		perror("epoll_ctl");
		LogPrint("epoll_ctl add failed\n");
		CloseHandle(hl, he);
		LogPop();
		return NULL;
	}

//	debugPrint("}AcceptTCPClient\n");
	//1 LogPrint("Client accepted\n");
	LogPop();
	return he;
}


HandleEntry	*AddTCPClient2(HandleList *hl, char *remotehost, WORD remoteport,
	char *name, void *callback, void *ed) {
	HandleEntry		*he;
	struct	sockaddr_in	LocalAddr;
	int			k, pollres;

//	LogPush("ADDTCPCLIENT");
//	LogPrint("0\n");
	he = GetHandle(hl);
//	LogPrint("0.0\n");
	if (!he) {
		LogPrint("No handle\n");
		//LogPop();
		return NULL;
	}
//	LogPrint("0.1\n");
	bzero(((char *) he)+sizeof(llist_entry), sizeof(HandleEntry)-sizeof(llist_entry));
//LogPrint("1\n");
	he->fhin = socket(PF_INET, SOCK_STREAM, 0);
	if (he->fhin < 0) {
		LogPrint("socket error\n");
		CloseHandle(hl, he);
		//LogPop();
		return NULL;
	}
	bzero(&LocalAddr, sizeof(LocalAddr));
	LocalAddr.sin_family = AF_INET;
	LocalAddr.sin_addr.s_addr = inet_addr(remotehost);
	LocalAddr.sin_port = htons(remoteport);

	//strcpy(he->IpAddress, (char *) inet_ntoa(LocalAddr.sin_addr));
	strcpy(he->RemoteAddress, remotehost);
	he->rIP = LocalAddr.sin_addr.s_addr;
	he->rPort = LocalAddr.sin_port;
	he->Port = remoteport;
	he->ExtraData = ed;
	fcntl(he->fhin, F_SETFL, O_NONBLOCK
	// | fcntl(he->fhin, F_GETFL, 0)
	);
	
	if (LocalAddr.sin_addr.s_addr == INADDR_NONE) {
		LogPrint("No name must resolve!\n");
	} else {
		
		strcpy(he->IpAddress, (char *) inet_ntoa(LocalAddr.sin_addr));
		if (connect(he->fhin, (struct sockaddr *) &LocalAddr, sizeof(LocalAddr)) < 0) {
			if (errno != EINPROGRESS) {
				//perror("Fast connect:");
			//	LogPrint("FAST ERROR [%d]\n", errno);
				CloseHandle(hl, he);
			//	LogPop();
				return NULL;
			} else {
				//perror("Normal connect:");
				;//LogPrint("NORMAL CONNECT [%d]\n", errno);
			}
		}
	}	
	strcpy(he->name, name);
	he->CallBack = callback;
	he->inuse = 1;
	he->Timeout = 0;
	he->EventTimeout = 0;
	he->Connecting = 1;
	//he->idx = ++hl->startidx;
	he->handletype = CLIENT_HANDLE;
	he->fhout = he->fhin;
	
	k = he->le.ix;
	hl->polls[k].events = EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLOUT;
	hl->polls[k].data.ptr = he;
	he->pf = hl->polls+k;
	he->pollfd = hl->pollfd;

	if (he->rIP != INADDR_ANY && he->rIP != INADDR_NONE) {
		
		if ((pollres = epoll_ctl(he->pollfd, EPOLL_CTL_ADD, he->fhin, he->pf)) < 0) {
			LogPrint("epoll_ctl add failed\n");
			CloseHandle(hl, he);
			//LogPop();
			return NULL;
		}
		
	} else {
		strcpy(he->IpAddress, remotehost);
	}
	if (callback)
		(*he->CallBack)(hl, he, EVENT_CREATE, NULL, 0, NULL);
//	LogPop();
	return he;
}


HandleEntry	*AddTCPClient6(HandleList *hl, char *remotehost, WORD remoteport,
	char *name, void *callback, void *ed, char *intf) {
	HandleEntry		*he;
	struct	sockaddr_in6	LocalAddr;
	int			k, pollres;

//	LogPush("ADDTCPCLIENT");
//	LogPrint("0\n");
	he = GetHandle(hl);
//	LogPrint("0.0\n");
	if (!he) {
		LogPrint("No handle\n");
		//LogPop();
		return NULL;
	}
//	LogPrint("0.1\n");
	bzero(((char *) he)+sizeof(llist_entry), sizeof(HandleEntry)-sizeof(llist_entry));
//LogPrint("1\n");
	he->fhin = socket(PF_INET6, SOCK_STREAM, 0);
	he->ExtraData = ed;
	if (he->fhin < 0) {
		LogPrint("socket error\n");
		CloseHandle(hl, he);
		//LogPop();
		return NULL;
	}
	bzero(&LocalAddr, sizeof(LocalAddr));
	LocalAddr.sin6_family = AF_INET6;
	inet_pton(AF_INET6, remotehost, &LocalAddr.sin6_addr);
	//LocalAddr.sin6_addr.s_addr = inet_addr(remotehost);

	LocalAddr.sin6_scope_id = if_nametoindex(intf);
	LocalAddr.sin6_port = htons(remoteport);

	inet_ntop(AF_INET6, &LocalAddr.sin6_addr, he->IpAddress, sizeof(he->IpAddress) - 2);
	//strcpy(he->IpAddress, (char *) inet_ntoa(LocalAddr.sin_addr));
	strcpy(he->RemoteAddress, remotehost);
	//memcpy(he->IpAddress, )
	
	he->rIP = 0;
	memcpy(he->rIP6, LocalAddr.sin6_addr.__in6_u.__u6_addr8, 16);
	he->rPort = LocalAddr.sin6_port;
	he->Port = remoteport;
	he->ExtraData = ed;
	fcntl(he->fhin, F_SETFL, O_NONBLOCK
	// | fcntl(he->fhin, F_GETFL, 0)
	);
	
	//if (LocalAddr.sin6_addr.s_addr == INADDR_NONE) {
	//	LogPrint("No name must resolve!\n");
	//} else 
	{
		inet_ntop(LocalAddr.sin6_family, &LocalAddr.sin6_addr, he->IpAddress, sizeof(he->IpAddress));
		//strcpy(he->IpAddress, (char *) inet_ntoa(LocalAddr.sin_addr));
		if (connect(he->fhin, (struct sockaddr *) &LocalAddr, sizeof(LocalAddr)) < 0) {
			if (errno != EINPROGRESS) {
				int	err = errno;
				//perror("Fast connect:");
			//	LogPrint("FAST ERROR [%d]\n", errno);
				CloseHandle(hl, he);
			//	LogPop();
				return NULL;
			} else {
				//perror("Normal connect:");
				;//LogPrint("NORMAL CONNECT [%d]\n", errno);
			}
		}
	}	
	strcpy(he->name, name);
	he->CallBack = callback;
	he->inuse = 1;
	he->Timeout = 0;
	he->EventTimeout = 0;
	he->Connecting = 1;
	//he->idx = ++hl->startidx;
	he->handletype = CLIENT_HANDLE;
	he->fhout = he->fhin;
	
	k = he->le.ix;
	hl->polls[k].events = EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLOUT;
	hl->polls[k].data.ptr = he;
	he->pf = hl->polls+k;
	he->pollfd = hl->pollfd;

	//if (he->rIP != INADDR_ANY && he->rIP != INADDR_NONE) 
	{
		
		if ((pollres = epoll_ctl(he->pollfd, EPOLL_CTL_ADD, he->fhin, he->pf)) < 0) {
			LogPrint("epoll_ctl add failed\n");
			CloseHandle(hl, he);
			//LogPop();
			return NULL;
		}
		
	} /*else {
		strcpy(he->IpAddress, remotehost);
	}*/
	if (callback)
		(*he->CallBack)(hl, he, EVENT_CREATE, NULL, 0, NULL);
//	LogPop();
	return he;
}


HandleEntry	*AddUDPServer(HandleList *hl, int port, void *callback) {
	HandleEntry		*he;
	struct	sockaddr_in	LocalAddr;
	int			i, k, pollres;

	LogPush("AddUDPServer");

	he = GetHandle(hl);
	if (!he) {
		LogPrint("No handle!\n");
		LogPop();
		return NULL;
	}
	bzero(((char *) he)+sizeof(llist_entry), sizeof(HandleEntry)-sizeof(llist_entry));

	he->fhin = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (he->fhin < 0) {
		LogPrint("No socket!\n");
		goto l0;
	}
	bzero(&LocalAddr, sizeof(LocalAddr));
	LocalAddr.sin_family = AF_INET;
	LocalAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	LocalAddr.sin_port = htons(port);
	i = 1;
	setsockopt(he->fhin, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
	if (bind(he->fhin,
		(struct sockaddr *) &LocalAddr,
		sizeof(LocalAddr))
	) {
		LogPrint("No bind!\n");
		goto l1;
	}
//	LogPrint("UDP portu (%d) acildi\n", port);
	he->CallBack = callback;
	he->inuse = 1;
	he->Timeout = 0;
	he->EventTimeout = 0;
	he->Port = port;
	//he->idx = ++hl->startidx;
	he->handletype = UDP_HANDLE;
	if (he->CallBack) {
		int	r;
		if ((r = (*he->CallBack)(hl, he, EVENT_CREATE, NULL, 0, he->ExtraData))) {
			goto l0;
		}
	}
	he->pf = NULL;
	k = he->le.ix;
	hl->polls[k].events = EPOLLIN | EPOLLHUP | EPOLLERR;
	hl->polls[k].data.ptr = he;
	he->pf = hl->polls+k;
	he->pollfd = hl->pollfd;
	if ((pollres = epoll_ctl(he->pollfd, EPOLL_CTL_ADD, he->fhin, he->pf)) < 0) {
		perror("epoll_ctl");
		goto l0;
	}
	LogPrint("AddUDPServer %u done\n", port);
	LogPop();
	return he;
l1:
	close(he->fhin);
	he->fhin = 0;
l0:
	CloseHandle(hl, he);
	LogPrint("Failed\n");
	LogPop();
	return NULL;
}




HandleEntry	*AddTCPClient(HandleList *hl, char *remotehost, WORD remoteport, char *name, void *callback) {
	return AddTCPClient2(hl, remotehost, remoteport, name, callback, NULL);
}


HandleEntry	*AddLineTCPClient(HandleList *hl, char *remotehost, WORD remoteport, char *name, void *callback) {
	HandleEntry			*he = NULL;
	_processlinecontext	*lc;
	LogPrint("AddLineTCPClient\n");
	lc = calloc(sizeof(_processlinecontext), 1);
	if (!lc)
		goto l0;
	lc->cb = callback;
	LogPrint("AddLineTCPClient\n");
	he = AddTCPClient2(hl, remotehost, remoteport, name, _ProcessLineCB, lc);
	LogPrint("AddLineTCPClient\n");
	if (he)
		goto l0;
	LogPrint("AddLineTCPClient\n");
	free(lc);
l0:
LogPrint("AddLineTCPClient %p\n", he);
	return he;
}

int     _ProcessLineCBs(HandleList *hl, HandleEntry *he, int cmd, char *data, int len, char *ExtraData);

HandleEntry	*AddLineTCPClientS(HandleList *hl, char *remotehost, WORD remoteport, char *name, void *callback) {
	HandleEntry			*he = NULL;
	_processlinecontext	*lc;
	lc = calloc(sizeof(_processlinecontext), 1);
	if (!lc)
		goto l0;
	lc->cb = callback;
	he = AddTCPClient2(hl, remotehost, remoteport, name, _ProcessLineCBs, lc);
	
	if (he)
		goto l0;
	free(lc);
l0:
	return he;
}

int     GetUDPBcast(int port) {
        struct  sockaddr_in     LocalAddr;
        int                     i;
        int                     fhin;
		int						optlen;

        fhin = socket(PF_INET, SOCK_DGRAM, 0);
        if (fhin < 0) {
                LogPrint("No socket!\n");
                goto l0;
        }
        bzero(&LocalAddr, sizeof(LocalAddr));
        LocalAddr.sin_family = AF_INET;
		
        LocalAddr.sin_addr.s_addr = 0xffffffff; // htonl(INADDR_ANY);
//		inet_aton("192.168.124.206", (struct in_addr *) &LocalAddr.sin_addr.s_addr);
        LocalAddr.sin_port = htons(port);
        i = 1;

        if (bind(fhin,
                (struct sockaddr *) &LocalAddr,
                sizeof(LocalAddr))
        ) {
                LogPrint("No bind!\n");
				perror("BIND ERROR"); 
//              LogPop();
                goto l1;
        }
        i = 1;
        setsockopt(fhin, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
        i = 1;
        setsockopt(fhin, SOL_SOCKET, SO_BROADCAST, &i, sizeof(i));
	i = 0;
        getsockopt(fhin, SOL_SOCKET, SO_BROADCAST, (void *) &i, (socklen_t *) &optlen);
	LogPrint("BROADCAST OPT IS SET TO %d\r\n", i);
       	
//        LogPrint("UDP portu (%d) acildi %d\n", port, i);
        return fhin;
l1:    
        close(fhin);
l0:
        return 0;
}


int     GetUDPBcastToA(int port, char *addr) {
        struct  sockaddr_in     LocalAddr;
        int                     i;
        int                     fhin;
	int			optlen;

        fhin = socket(PF_INET, SOCK_DGRAM, 0);
        if (fhin < 0) {
                LogPrint("No socket!\n");
                goto l0;
        }
        bzero(&LocalAddr, sizeof(LocalAddr));
        LocalAddr.sin_family = AF_INET;
		
      	if (addr[0] >= '1' && addr[0] <= '9')
		LocalAddr.sin_addr.s_addr = inet_addr(addr);
	else
		LocalAddr.sin_addr.s_addr = 0xffffffff; // htonl(INADDR_ANY);
	if (addr[0] <= 'z' && addr[0] >= 'a') {
		struct ifreq ifr;
		memset(&ifr, 0, sizeof(ifr));
		snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", addr);
		if (setsockopt(fhin, SOL_SOCKET, SO_BINDTODEVICE, (void *)&ifr, sizeof(ifr)) < 0) {
			LogPrint("Bind to [%s] is failed\r\n");
		}
	}
//		inet_aton("192.168.124.206", (struct in_addr *) &LocalAddr.sin_addr.s_addr);
        LocalAddr.sin_port = htons(port);
        i = 1;

        if (bind(fhin,
                (struct sockaddr *) &LocalAddr,
                sizeof(LocalAddr))
        ) {
                LogPrint("No bind!\n");
				perror("BIND ERROR"); 
//              LogPop();
                goto l1;
        }
        i = 1;
        setsockopt(fhin, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
        i = 1;
        setsockopt(fhin, SOL_SOCKET, SO_BROADCAST, &i, sizeof(i));
	i = 0;
        getsockopt(fhin, SOL_SOCKET, SO_BROADCAST, (void *) &i, (socklen_t *) &optlen);
	LogPrint("BROADCAST OPT IS SET TO %d\r\n", i);
       	
//        LogPrint("UDP portu (%d) acildi %d\n", port, i);
        return fhin;
l1:    
        close(fhin);
l0:
        return 0;
}


int     GetUDPToA_SOCK(int port, char *addr, struct sockaddr_in *LocalAddr, int isbind) {
	
	int                     i;
	int                     fhin;
	int				optlen;

	fhin = socket(PF_INET, SOCK_DGRAM, 0);
      if (fhin < 0) {
		LogPrint("No socket!\n");
		goto l0;
	}
	bzero(LocalAddr, sizeof(*LocalAddr));
	LocalAddr->sin_family = AF_INET;
		
      if (addr[0] >= '1' && addr[0] <= '9')
		LocalAddr->sin_addr.s_addr = inet_addr(addr);
	else
		LocalAddr->sin_addr.s_addr = 0xffffffff; 

	if (addr[0] <= 'z' && addr[0] >= 'a') {
		struct ifreq ifr;
		memset(&ifr, 0, sizeof(ifr));
		snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", addr);
		if (setsockopt(fhin, SOL_SOCKET, SO_BINDTODEVICE, (void *)&ifr, sizeof(ifr)) < 0) {
			LogPrint("Bind to [%s] is failed\r\n");
		}
		//goto l2;
	}
//		inet_aton("192.168.124.206", (struct in_addr *) &LocalAddr.sin_addr.s_addr);
      LocalAddr->sin_port = htons(port);
	i = 1;
	if (isbind) {
		if (bind(fhin,
			(struct sockaddr *) LocalAddr,
			sizeof(LocalAddr))
		) {
			LogPrint("No bind!\n");
			perror("BIND ERROR"); 
	//              LogPop();
			goto l1;
		} else {
			LogPrint("UDP bind %d -> %s:%d\r\n", fhin, inet_ntoa(LocalAddr->sin_addr), htons(LocalAddr->sin_port));
		}
	}
//l2:
	i = 1;
	setsockopt(fhin, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
	return fhin;
l1:    
	close(fhin);
l0:
	return 0;
}

int     GetUDPToA(int port, char *addr) {
	struct  sockaddr_in     LocalAddr;
	return GetUDPToA_SOCK(port, addr, &LocalAddr, 1);
}

int     GetAUDPBcast(int port, struct  sockaddr_in *LocalAddr) {
//        struct  sockaddr_in     LocalAddr;
        int                     i;
        int                     fhin;
		int						optlen;

        fhin = socket(PF_INET, SOCK_DGRAM, 0);
        if (fhin < 0) {
                printf("No socket!\n");
                goto l0;
        }
        bzero(LocalAddr, sizeof(*LocalAddr));
        LocalAddr->sin_family = AF_INET;
		
        LocalAddr->sin_addr.s_addr = 0xffffffff; // htonl(INADDR_ANY);
//		inet_aton("192.168.124.206", (struct in_addr *) &LocalAddr.sin_addr.s_addr);
        LocalAddr->sin_port = htons(port);
        i = 1;

        if (bind(fhin,
                (struct sockaddr *) LocalAddr,
                sizeof(*LocalAddr))
        ) {
                printf("No bind!\n");
				perror("BIND ERROR"); 
//              LogPop();
                goto l1;
        }
        i = 1;
        setsockopt(fhin, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
        i = 1;
        setsockopt(fhin, SOL_SOCKET, SO_BROADCAST, &i, sizeof(i));
        getsockopt(fhin, SOL_SOCKET, SO_BROADCAST, (void *) &i, (socklen_t *) &optlen);
	LogPrint("BROADCAST OPT IS SET TO %d\r\n", i);
       // printf("UDP portu (%d) acildi %d\n", port, i);
        return fhin;
l1:    
        close(fhin);
l0:
        return 0;
}

HandleEntry	*AddUDPBCServerToA(HandleList *hl, int port, void *callback, char *addr) {
	HandleEntry		*he;
//	struct	sockaddr_in	LocalAddr;
	int			k, pollres; // i

	LogPush("AddUDPServer");

	he = GetHandle(hl);
	if (!he) {
		//debugPrint("} AddTCPServer.no handle\n");
		LogPrint("No handle!\n");
		LogPop();
		return NULL;
	}
	bzero(((char *) he)+sizeof(llist_entry), sizeof(HandleEntry)-sizeof(llist_entry));

	he->fhin = GetUDPBcastToA(port, addr);//GetUDPBcast(port); //GetUDPSock(port);
	if (he->fhin <= 0) {
		LogPrint("No socket!\n");
//		LogPop();
		goto l0;
	}
	
	//LogPrint("UDP portu (%d) acildi\n", port);
	he->CallBack = callback;
	he->inuse = 1;
	he->Timeout = 0;
	he->EventTimeout = 0;
	//he->idx = ++hl->startidx;
	he->handletype = UDP_HANDLE;
	if (he->CallBack) {
		int	r;
		if ((r = (*he->CallBack)(hl, he, EVENT_CREATE, NULL, 0, he->ExtraData))) {
			//CloseHandle(hl, he);
			goto l0;
		}
	}
	he->pf = NULL;
	k = he->le.ix;
	hl->polls[k].events = EPOLLIN | EPOLLHUP | EPOLLERR;
	hl->polls[k].data.ptr = he;
	he->pf = hl->polls+k;
	he->pollfd = hl->pollfd;
	if ((pollres = epoll_ctl(he->pollfd, EPOLL_CTL_ADD, he->fhin, he->pf)) < 0) {
		perror("epoll_ctl");
		//CloseHandle(hl, he);
		goto l0;
	}
	LogPrint("AddUDPBCServer done\n");
	LogPop();
	return he;
//l1:
	close(he->fhin);
	he->fhin = 0;
l0:
	CloseHandle(hl, he);
	LogPrint("Failed\n");
	LogPop();
	return NULL;
}


HandleEntry	*AddUDPBCServer(HandleList *hl, int port, void *callback) {
	return AddUDPBCServerToA(hl, port, callback, "0.0.0.0");
}

int     GetUDPSock(int port) {
	struct  sockaddr_in     LocalAddr;
	int                     i;
	int                     fhin;
	int						optlen;

	fhin = socket(PF_INET, SOCK_DGRAM, 0);
	if (fhin < 0) {
			LogPrint("No socket!\n");
			goto l0;
	}
	bzero(&LocalAddr, sizeof(LocalAddr));
	LocalAddr.sin_family = AF_INET;
	
	LocalAddr.sin_addr.s_addr = htonl(INADDR_ANY);
//		inet_aton("192.168.124.206", (struct in_addr *) &LocalAddr.sin_addr.s_addr);
	LocalAddr.sin_port = htons(port);
	i = 1;

	setsockopt(fhin, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));

	if (bind(fhin,
			(struct sockaddr *) &LocalAddr,
			sizeof(LocalAddr))
	) {
			LogPrint("No bind!\n");
			perror("BIND ERROR"); 
//              LogPop();
			goto l1;
	}
	i = 1;
	
//	LogPrint("UDP portu (%d) acildi %d\n", port, i);
	return fhin;
l1:    
	close(fhin);
l0:
	return 0;
}


HandleEntry	*AddUDPClient(HandleList *hl, char *remotehost, int port, char *name, void *callback) {
	HandleEntry		*he;
	int			k, pollres;
	
	LogPush("Add UDP Client");

	he = GetHandle(hl);
	if (!he) {
		LogPrint("No handle!\n");
		LogPop();
		return NULL;
	}
	bzero(((char *) he)+sizeof(llist_entry), sizeof(HandleEntry)-sizeof(llist_entry));

	he->fhin = GetUDPSock(port);
	if (he->fhin <= 0) {
		LogPrint("No socket!\n");
		goto l0;
	}
	fcntl(he->fhin, F_SETFL, O_NONBLOCK
	// | fcntl(he->fhin, F_GETFL, 0)
	);
	
//	LogPrint("UDP portu (%d) acildi\n", port);
	he->CallBack = callback;
	he->inuse = 1;
	he->Timeout = 0;
	he->EventTimeout = 0;
	//he->idx = ++hl->startidx;
	he->handletype = UDP_HANDLE;
	
	he->pf = NULL;
	he->Port = port;
	k = he->le.ix;
	strcpy(he->name, name);
	if (he->CallBack) {
		int	r;
		if ((r = (*he->CallBack)(hl, he, EVENT_CREATE, NULL, 0, he->ExtraData))) {
			goto l0;
		}
	}
	hl->polls[k].events = EPOLLIN;
	hl->polls[k].data.ptr = he;
	he->pf = hl->polls+k;
	he->pollfd = hl->pollfd;
	if ((pollres = epoll_ctl(he->pollfd, EPOLL_CTL_ADD, he->fhin, he->pf)) < 0) {
		perror("epoll_ctl");
		goto l0;
	}
	//LogPrint("Add UDP client done [%s]\n", he->name);
	LogPop();
	return he;
l0:
	CloseHandle(hl, he);
	LogPrint("Failed\n");
	LogPop();
	return NULL;
}


HandleEntry	*AddUDPClient_tomem(HandleList *hl, char *remotehost, int port, char *name, void *callback) {
	HandleEntry		*he;
	
	if ((he = AddUDPClient(hl, remotehost, port, name, callback))) {
		he->handletype = UDP_HANDLE_TOMEM;
	}

	return he;
}

HandleEntry	*AddTCPClientToHost(HandleList *hl, char *remotehost, WORD remoteport, char *name, void *callback) {
	struct	sockaddr_in	LocalAddr;
	char	ipAddress[64];
	LocalAddr.sin_addr.s_addr = inet_addr(remotehost);
	LocalAddr.sin_port = htons(remoteport);
	strcpy(ipAddress, remotehost);
	if (LocalAddr.sin_addr.s_addr == INADDR_NONE) {
		return NULL;
	}

	//strcpy(ipAddress, (char *) inet_ntoa(LocalAddr.sin_addr));
	return AddTCPClient2(hl, ipAddress, remoteport, name, callback, NULL);
}


#define T_A 1 //Ipv4 address
#define T_NS 2 //Nameserver
#define T_CNAME 5 // canonical name
#define T_SOA 6 /* start of authority zone */
#define T_PTR 12 /* domain name pointer */
#define T_MX 15 //Mail server
 
#pragma	pack(1)
//DNS header structure
struct DNS_HEADER
{
    unsigned short id; // identification number
 
    unsigned char rd :1; // recursion desired
    unsigned char tc :1; // truncated message
    unsigned char aa :1; // authoritive answer
    unsigned char opcode :4; // purpose of message
    unsigned char qr :1; // query/response flag
 
    unsigned char rcode :4; // response code
    unsigned char cd :1; // checking disabled
    unsigned char ad :1; // authenticated data
    unsigned char z :1; // its z! reserved
    unsigned char ra :1; // recursion available
 
    unsigned short q_count; // number of question entries
    unsigned short ans_count; // number of answer entries
    unsigned short auth_count; // number of authority entries
    unsigned short add_count; // number of resource entries
};
 
//Constant sized fields of query structure
struct QUESTION
{
    unsigned short qtype;
    unsigned short qclass;
};
 
//Constant sized fields of the resource record structure

struct R_DATA
{
    unsigned short type;
    unsigned short _class;
    unsigned int ttl;
    unsigned short data_len;
};
 

//Pointers to resource record contents
struct RES_RECORD
{
    unsigned char *name;
    struct R_DATA *resource;
    unsigned char *rdata;
};
 
//Structure of a Query
typedef struct
{
    unsigned char *name;
    struct QUESTION *ques;
} QUERY;


typedef struct {
	WORD	id;
	
} dnsQ;

//#pragma pack(pop)

void ChangetoDnsNameFormat(unsigned char* dns,unsigned char* host) 
{
    int lock = 0 , i;
    strcat((char*)host,".");
     
    for(i = 0 ; i < strlen((char*)host) ; i++) 
    {
        if(host[i]=='.') 
        {
            *dns++ = i-lock;
            for(;lock<i;lock++) 
            {
                *dns++=host[lock];
            }
            lock++; //or lock=i+1;
        }
    }
    *dns++='\0';
}

void ngethostbyname(HandleList *hl, HandleEntry *he, char *host, int query_type) {
    unsigned char buf[65536],*qname,*reader;
    int i , j , stop ;
 
 struct sockaddr_in dest;
   dest.sin_family = AF_INET;
    dest.sin_port = htons(53);
    dest.sin_addr.s_addr = inet_addr("8.8.8.8"); //dns servers
 
    
    struct RES_RECORD answers[20],auth[20],addit[20]; //the replies from the DNS server
    
 
    struct DNS_HEADER *dns = NULL;
    struct QUESTION *qinfo = NULL;
 
   // LogPrint("Resolving \"%s\"\n" , host);
 
    
    //Set the DNS structure to standard queries
    dns = (struct DNS_HEADER *)&buf;
 
    dns->id = (unsigned short) htons(getpid());
    dns->qr = 0; //This is a query
    dns->opcode = 0; //This is a standard query
    dns->aa = 0; //Not Authoritative
    dns->tc = 0; //This message is not truncated
    dns->rd = 1; //Recursion Desired
    dns->ra = 0; //Recursion not available! hey we dont have it (lol)
    dns->z = 0;
    dns->ad = 0;
    dns->cd = 0;
    dns->rcode = 0;
    dns->q_count = htons(1); //we have only 1 question
    dns->ans_count = 0;
    dns->auth_count = 0;
    dns->add_count = 0;
 
    //point to the query portion
    qname =(unsigned char*)&buf[sizeof(struct DNS_HEADER)];
 
    ChangetoDnsNameFormat(qname , host);
    qinfo =(struct QUESTION*)&buf[sizeof(struct DNS_HEADER) + (strlen((const char*)qname) + 1)]; //fill it
 
    qinfo->qtype = htons( query_type ); //type of the query , A , MX , CNAME , NS etc
    qinfo->qclass = htons(1); //its internet (lol)
 
    //LogPrint("Sending Packet...\n");
    if( sendto(he->fhin,(char*)buf,sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION),0,(struct sockaddr*)&dest, sizeof(dest)) < 0)
    {
    	;//LogPrint("Sendto Failed...\n");
    }
    //LogPrint("Done\n");
}


u_char* ReadName(unsigned char* reader,unsigned char* buffer, int* count) {
	static    unsigned char	bname[256];
	unsigned char	*name;
    unsigned int	p=0,jumped=0,offset;
    int i , j;
 
    *count = 1;
    name = (unsigned char*) bname;
 
    name[0]='\0';
 
    //read the names in 3www6google3com format
    while(*reader!=0) {
        if(*reader>=192) {
            offset = (*reader)*256 + *(reader+1) - 49152; //49152 = 11000000 00000000 ;)
            reader = buffer + offset - 1;
            jumped = 1; //we have jumped to another location so counting wont go up!
        } else {
            name[p++]=*reader;
        }
 
        reader = reader+1;
 
        if(jumped==0) {
            *count = *count + 1; //if we havent jumped to another location then we can count up
        }
    }
 
    name[p]='\0'; //string complete
    if(jumped==1)
    {
        *count = *count + 1; //number of steps we actually moved forward in the packet
    }
 
    //now convert 3www6google3com0 to www.google.com
    for(i=0;i<(int)strlen((const char*)name);i++) {
        p=name[i];
        for(j=0;j<(int)p;j++) 
        {
            name[i]=name[i+1];
            i=i+1;
        }
        name[i]='.';
    }
    name[i-1]='\0'; //remove the last dot
    return name;
}

int     cbDNS(HandleList *hl, HandleEntry *he, int cmd, char *data, int len, char *ExtraData) {
	int				r;
	static char		line[512];
	static int		lp = 0;
	static	char	*flds[32];
	static	int		fldc;

	switch (cmd) {
		case EVENT_WRITE:
			LogPrint("EVENT_WRITE\n");
			ngethostbyname(hl, he, he->name, T_A);
			return WPDoWrite(hl, he);
		case EVENT_CREATE:
			he->EventTimeout = 30000;
			break;
		case EVENT_TIMEOUT: {
			char	name[64];
			strcpy(name, he->name);
			ngethostbyname(hl, he, name, T_A);
			he->Timeout = 5000000;
			he->EventTimeout = 2000000;
			}
			break;
		case EVENT_FREE: {
				e_CallBack	cb = (e_CallBack) he->ExtraData;
				//LogPrint("REQ FREE %s\n", he->name);		
				if (cb)
					(*cb)(hl, he, EVENT_RESOLVEDONE, NULL, 0, NULL);
			}
           	break;
		case EVENT_DATA: {
			unsigned char	*qname,*reader;
			struct sockaddr_in a;
    			struct RES_RECORD answers[20],auth[20],addit[20]; //the replies from the DNS server
			struct DNS_HEADER *dns = NULL;
			struct QUESTION *qinfo = NULL;
			int	l, i, j, stop;

			l = * (int *) data;
			data += 4+l;
			len -= 4+l;
			dns = (struct DNS_HEADER*) data;
			qname =(unsigned char*)&data[sizeof(struct DNS_HEADER)];
 
		    	reader = &data[sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION)];

	 		stop=0;
 
    			for(i=0; i<ntohs(dns->ans_count); i++) {
				char	buf[512];
				answers[i].name=ReadName(reader,data,&stop);
				reader = reader + stop;
 
				answers[i].resource = (struct R_DATA*)(reader);
				reader = reader + sizeof(struct R_DATA);
		
        			if (ntohs(answers[i].resource->type) == 1) {//if its an ipv4 address
            			answers[i].rdata = (unsigned char*) buf;
		
					for(j=0 ; j<ntohs(answers[i].resource->data_len) ; j++) {
						answers[i].rdata[j]=reader[j];
					}
 
					answers[i].rdata[ntohs(answers[i].resource->data_len)] = '\0';
		
					reader = reader + ntohs(answers[i].resource->data_len);
				} else {
					answers[i].rdata = ReadName(reader,data,&stop);
					reader = reader + stop;
				}
    			}
 
			for(i=0 ; i < ntohs(dns->ans_count) ; i++) {		
				if( ntohs(answers[i].resource->type) == T_A) {//IPv4 address
					BYTE	*p;
					char	ServerIP[32];
					p=(BYTE *) answers[i].rdata;
					sprintf(ServerIP, "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
					 {
						e_CallBack	cb = (e_CallBack) he->ExtraData;
						if (cb)
							(*cb)(hl, he, EVENT_RESOLVED, ServerIP, strlen(ServerIP), NULL);
					}

					return 1;
				}
			
			}
			return 1;
		}
		default:
			//LogPrint("REQ EVENT %d\n", cmd);
            ;
	}
	return 0;
}

HandleEntry	*DNSQuery(HandleList *hl, char *name, void *callback) {
	HandleEntry	*he = AddUDPClient(hl, "8.8.8.8", 20000, name, cbDNS);
	if (he)
		he->ExtraData = callback;
	return he;
}

int     GetONVIFSock(int port, char *addr, char *intf) {
	struct  sockaddr_in     LocalAddr;
	int                     i;
	int                     fhin;
	int				optlen;
	struct ip_mreqn 		mreq;

	fhin = socket(PF_INET, SOCK_DGRAM, 0);
	if (fhin < 0) {
		LogPrint("No socket!\n");
		goto l0;
	}
	i = 0;
	if ((i = setsockopt(fhin, IPPROTO_IP, IP_MULTICAST_LOOP, &i, sizeof(i)))) {
		LogPrint("IP_MULTICAST_LOOP error!\n");
		goto l1;
	}
	i = 16;
	if ((i = setsockopt(fhin, IPPROTO_IP, IP_MULTICAST_TTL, &i, sizeof(i)))) {
		LogPrint("IP_MULTICAST_TTL error!\n");
		goto l1;
	}
	bzero(&mreq, sizeof(mreq));
	mreq.imr_multiaddr.s_addr = inet_addr(addr);
	mreq.imr_address.s_addr = INADDR_ANY;
	

	if ((i = setsockopt(fhin, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)))) {
		LogPrint("IP_ADD_MEMBERSHIP error!\n");
		goto l1;
	} 
	
	if (intf) 
		if (*intf) {
			struct in_addr interface_addr;
			bzero(&interface_addr, sizeof(interface_addr));
			interface_addr.s_addr = inet_addr(intf);
			i=setsockopt(fhin, SOL_SOCKET, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr));
		}
	i = 1;
i = setsockopt(fhin, SOL_SOCKET, SO_BROADCAST, &i, sizeof(i));

//        LogPrint("UDP portu (%d) acildi %d\n", port, i);
	return fhin;
l1:    
	close(fhin);
l0:
	return 0;
}

HandleEntry	*AddOnvifListener(HandleList *hl, int port, void *callback, char *addr, char *intf) {
	HandleEntry			*he;
	int				k, pollres; 
	struct sockaddr_in	LocalAddr;

	LogPush("AddOnvifListener");

	he = GetHandle(hl);
	if (!he) {
		//debugPrint("} AddTCPServer.no handle\n");
		LogPrint("No handle!\n");
		LogPop();
		return NULL;
	}
	bzero(((char *) he)+sizeof(llist_entry), sizeof(HandleEntry)-sizeof(llist_entry));
	he->Port = port;
	he->fhin = GetONVIFSock(port, addr, intf);
	
	if (he->fhin <= 0) {
		LogPrint("No socket!\n");
//		LogPop();
		goto l0;
	}
	
	//LogPrint("UDP portu (%d) acildi\n", port);
	he->CallBack = callback;
	he->inuse = 1;
	he->Timeout = 0;
	he->EventTimeout = 0;
	//he->idx = ++hl->startidx;
	
	bzero(&LocalAddr, sizeof(LocalAddr));
	LocalAddr.sin_family = AF_INET;
	LocalAddr.sin_addr.s_addr = INADDR_ANY;
	LocalAddr.sin_port = htons(port);
	if (bind(he->fhin,
		(struct sockaddr *) &LocalAddr,
		sizeof(LocalAddr))
	) {
		LogPrint("No bind!\n");
		goto l0;
	}

	he->pf = NULL;
	k = he->le.ix;
	hl->polls[k].events = EPOLLIN | EPOLLHUP | EPOLLERR;
	hl->polls[k].data.ptr = he;
	he->pf = hl->polls+k;
	he->pollfd = hl->pollfd;
	if ((pollres = epoll_ctl(he->pollfd, EPOLL_CTL_ADD, he->fhin, he->pf)) < 0) {
		perror("epoll_ctl");
		//CloseHandle(hl, he);
		goto l0;
	}
	he->handletype = UDP_HANDLE;
	if (he->CallBack) {
		int	r;
		if ((r = (*he->CallBack)(hl, he, EVENT_CREATE, NULL, 0, he->ExtraData))) {
			//CloseHandle(hl, he);
			goto l0;
		}
	}
	LogPrint("AddUDPBCServer done\n");
	LogPop();
	return he;
//l1:
	close(he->fhin);
	he->fhin = 0;
l0:
	CloseHandle(hl, he);
	LogPrint("Failed\n");
	LogPop();
	return NULL;
}

int	WSSendPong(HandleList *hl, HandleEntry *he, char *p, int l) {
	static	BYTE	PingF[64];
	
	PingF[0] = 0x8A;
	PingF[1] = 0x80 | l;
	PingF[2] = rand() & 0xff;
	PingF[3] = rand() & 0xff;
	PingF[4] = rand() & 0xff;
	PingF[5] = rand() & 0xff;
	
	for (int i = 0; i < l; i++) {
		PingF[6 + i] = p[i] ^ PingF[(i % 4) + 2];
	}
	//memcpy(PingF+6, p, l);
//	memcpy(PingF+2+sizeof(Sys.Now), &Sys.uNow, sizeof(Sys.uNow));
	if (!WPWrite(hl, WPADD_DYNAMIC_DATA, he, (char *) PingF, 6 + l))
		return WPDoWrite(hl, he);
	return 1;
}

