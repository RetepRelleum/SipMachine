#ifndef SipMachine_H
#define SipMachine_H
#include <Arduino.h>
#include "SipHeader.h"
#include "WiFiClient.h"
#include "WiFiServer.h"
#include "SdpHeader.h"
#include "WiFiUdp.h"
#include "RTP.h"
#include "SipStreamIn.h"


class SipMachine
{
private:
    unsigned long timeExpires = 0;
    unsigned long timeStOut=0;

    String user;
    String pwd;
    String telNr;
    String telNrTo;
    String userAgent;
    String userClient;
    String proxyServer;
    String proxyRegistrar;
    String tagTo;
    String authType="";
    int port = 5060;
    WiFiClient sock_sip;
    WiFiServer server;
    WiFiUDP udp;
    RTP rtpIn;
    RTP rtpOut;
    String branch = "z9hG4bK-" + randomChr(30);
    String tagFrom = randomChr(30);
    String callId = randomChr(7);
    String randomChr(int size);
    void parserSip(String in);
    void parserSdp(String in);
    void exec();
    void writeSIPdata(String message);
    void sipRegister();
    void sipRegisterAuth();
    void sipRinging();
    void sipOk();
    void sipInvite(String telNrTo);
    void sipAuth();
    void sipAck();
    void sipBye();
    void getDtmfData();
    void getSpeachData();
    void writeSpeachData();
    IPAddress strToIP(String str);
    SipHeader sipHeader;
    SdpHeader sdpHeader;
    String dtmf;
    IPAddress udpIpWrite;

public:
    typedef enum
    {
        init,
        reg,
        idle,
        ringIn,
        ringOut,
        callAccept,
        call,
    } Status;
    SipMachine(String user, String pwd, String telNr, String userAgent, String proxyRegistrar, int port = 5060);
    void setup(String userClient, String proxyServer);
    int16_t loop(int16_t pcmOut);
    String getTelNrIncomingCall();
    void acceptIncomingCall();
    void makeCall(String telNrTo);
    void bye();
    String getKeyPressedLast20();
    Status getStatus();
protected:
    Status status = init;
};

#endif
