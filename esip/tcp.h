/*
(C) 2009+ Ekrem Karacan
*/
#ifndef	TCPSERVER_H
#define	TCPSERVER_H

#include	"utils.h"

HandleEntry	*AddTCPServer(HandleList *hl, int port, void *callback);
HandleEntry	*AddTCPServerToIP(HandleList *hl, char *addr, int port, void *callback);
HandleEntry	*AddTCPServer6(HandleList *hl, int port, void *callback);
HandleEntry	*AddTCPServerToIPv6(HandleList *hl, char *addr, int port, void *callback);
HandleEntry	*AddTCPClient6(HandleList *hl, char *remotehost, WORD remoteport,
	char *name, void *callback, void *ed, char *intf);
HandleEntry	*AcceptTCPClient(HandleList *hl, HandleEntry *hep, char *name);
HandleEntry	*AddTCPClient(HandleList *hl, char *remotehost, WORD remoteport, char *name, void *callback);
HandleEntry	*AddTCPClient2(HandleList *hl, char *remotehost, WORD remoteport,
	char *name, void *callback, void *ed);
HandleEntry	*AddTCPClientToHost(HandleList *hl, char *remotehost, WORD remoteport, char *name, void *callback);
HandleEntry	*AddUDPServer(HandleList *hl, int port, void *callback);
HandleEntry	*AddLineTCPClient(HandleList *hl, char *remotehost, WORD remoteport, char *name, void *callback);
HandleEntry	*AddUDPBCServer(HandleList *hl, int port, void *callback);
HandleEntry	*AddUDPBCServerToA(HandleList *hl, int port, void *callback, char *addr);
int     GetUDPBcast(int port);
int     GetAUDPBcast(int port, struct  sockaddr_in *LocalAddr);
HandleEntry	*AddUDPClient(HandleList *hl, char *remotehost, int port, char *name, void *callback);
HandleEntry	*AddUDPClient_tomem(HandleList *hl, char *remotehost, int port, char *name, void *callback);
HandleEntry	*DNSQuery(HandleList *hl, char *name, void *callback);
int     cbDNS(HandleList *hl, HandleEntry *he, int cmd, char *data, int len, char *ExtraData);

HandleEntry	*AddLineTCPClientS(HandleList *hl, char *remotehost, WORD remoteport, char *name, void *callback);
int     GetUDPBcastToA(int port, char *addr);
int     GetUDPToA(int port, char *addr);
int     GetUDPToA_SOCK(int port, char *addr, struct sockaddr_in *LocalAddr, int bind);

HandleEntry	*AddOnvifListener(HandleList *hl, int port, void *callback, char *addr, char *intf);
int	WSSendPong(HandleList *hl, HandleEntry *he, char *p, int l);
#endif
