#include "SipMachine.h"
#include "Debug.h"

SipMachine::SipMachine(String user, String pwd, String telNr, String userAgent, String proxyRegistrar, int port)
{
    this->user = user;
    this->pwd = pwd;
    this->telNr = telNr;
    this->userAgent = userAgent;
    this->userClient = userClient;
    this->proxyRegistrar = proxyRegistrar;
    this->port = port;
    timeStOut = micros();
    timeStOut = timeStOut / 20000;
    timeStOut = timeStOut * 20000;
}

void SipMachine::setup(String userClient, String proxyServer)
{
    this->proxyServer = proxyServer;
    this->userClient = userClient;
    server.begin(port);

    debug_println(String(" Connect SIP Server ") + " Port " + String(port));

    udpIpWrite = strToIP(proxyServer);
}

IPAddress SipMachine::strToIP(String str)
{
    byte adr[4];
    adr[0] = str.substring(0, str.indexOf('.')).toInt();
    str = str.substring(str.indexOf('.') + 1);
    adr[1] = str.substring(0, str.indexOf('.')).toInt();
    str = str.substring(str.indexOf('.') + 1);
    adr[2] = str.substring(0, str.indexOf('.')).toInt();
    str = str.substring(str.indexOf('.') + 1);
    adr[3] = str.toInt();
    IPAddress ip(adr[0], adr[1], adr[2], adr[3]);
    return ip;
}

int16_t SipMachine::loop(short pcmOut)
{
    int ret;
    // write Data to outputbuffer
    if (status == call)
    {
        rtpOut.put(pcmOut);
    }
    if (timeStOut < micros())
    {
        timeStOut += 10000;
        if ((status == call) & (timeStOut % 20000 == 0))
        {
            ret = udp.parsePacket();
            debugL2_println(String("Nr. of parsePacket") + String(ret));
            if (ret)
            {
                if (ret == 16)
                {
                    getDtmfData();
                }
                else
                {
                    getSpeachData();
                }
            }
        }
        else
        {
            switch (status)
            {
            case init:
                if (timeExpires < millis())
                {
                    sipRegister();
                    status = reg;
                    timeExpires = millis() + 5000;
                }
                break;
            case reg:
                if (timeExpires < millis())
                {
                    sock_sip.stop();
                    debug_println(String(" Stop  SIP Client on IP ") + proxyServer + " Port " + String(port));
                    status = init;
                    timeExpires = millis() + 60000;
                }
                break;
            case idle:
                if ((timeExpires - millis() < 0))
                    status = init;
                break;
            case call:
                writeSpeachData();
                break;
            case ringIn:
                break;
            case ringOut:
                break;
            default:
                break;
            }
            if (sock_sip)
            {
                if (sock_sip.available())
                {
                    String str = sock_sip.readStringUntil('\n'); //terminator, buffer, sizeof(buffer));
                    if (sipHeader.contentLength > 0)
                    {
                        sipHeader.contentLength -= (str.length() + 1);
                        debugL1_println(str.c_str());
                        parserSdp(str);
                        if (sipHeader.contentLength < 0)
                        {
                            exec();
                        }
                    }
                    else
                    {
                        debugL1_print("-_ ");
                        debugL1_println(str.c_str());
                        parserSip(str);
                        if (str.equals("\r") || str.equals("\n"))
                            exec();
                    }
                }
            }
            else
            {
                sock_sip = server.available();
                if (sock_sip)
                {
                    debug_print("Creat new Sip client from Server on IP ");
                    debug_print(sock_sip.remoteIP().toString() + " and remote Port ");
                    debug_println(String(sock_sip.remotePort()));
                }
            }
        }
    }
    return rtpIn.get((micros() % 20000) / 125);
}

void SipMachine::parserSdp(String in)
{
    sdpHeader.parse(in);
}

void SipMachine::parserSip(String in)
{
    sipHeader.parse(in);
}

void SipMachine::exec()
{
    int ret;
    if (!sipHeader.to.telNr.equals(telNr))
    {
        tagTo = sipHeader.to.tagTo;
    }
    else
    {
        tagFrom = sipHeader.to.tagTo;
    }

    if (sipHeader.from.telNr.equals(telNr))
    {
        tagFrom = sipHeader.from.tagFrom;
    }
    else
    {
        tagTo = sipHeader.from.tagFrom;
    }
    switch (sipHeader.responseCodes)
    {
    case 0:
        debug_println("Execute ACK");
        sock_sip.stop();
        debug_println(String("sock_sip.stop"));
        ret = udp.begin(sdpHeader.udpPortRead);
        debug_println(String("Start UDP") + ((ret == 1) ? "TRUE" : "FALSE") + " on Port " + sdpHeader.udpPortRead);
        status = call;
        break;
    case 1:
        debug_println("Execute BYE");
        sipOk();
        sock_sip.stop();
        debug_println(String("sock_sip.stop"));
        udp.stop();
        debug_println(String("Stop UDP read "));
        status = idle;
        break;
    case 2:
        debug_println("Execute INVITE");
        sdpHeader.udpPortWrite = sdpHeader.m.port.toInt();

        sdpHeader.udpPortRead = sdpHeader.udpPortWrite + 10;
        sipRinging();
        status = ringIn;
        break;
    case 100:
        debug_println("Execute 100 Trying");
        break;
    case 180:
        debug_println("Execute 180 Ringing");
        status = ringOut;
        break;
    case 200:
        if (sipHeader.cSeq.typ.equals("REGISTER"))
        {
            debug_println("Execute 200 OK Register");
            timeExpires += sipHeader.contact.expires.toInt() * 1000;
            status = idle;
        }
        else if (sipHeader.cSeq.typ.equals("INVITE"))
        {
            debug_println("Execute 200 OK INVITE");
            sdpHeader.udpPortWrite = sdpHeader.m.port.toInt();
            sipAck();
            status = call;
            udpIpWrite = strToIP(sdpHeader.o.municastAddress);
        }
        sock_sip.stop();
        debug_println(String("sock_sip.stop"));
        break;
    case 401:
        debug_println("Execute 401 sipRegisterAuth");
        sipRegisterAuth();
        break;
    case 403:
        debug_println("Execute 403 Forbidden");
        sock_sip.stop();
        debug_println(String("sock_sip.stop"));
        udp.stop();
        debug_println(String("Stop UDP read and Write"));
        break;
    case 404:
        debug_println("Execute 404 Not Found");
        sock_sip.stop();
        debug_println(String("sock_sip.stop"));
        udp.stop();
        debug_println(String("Stop UDP read  and Write"));
        break;
    case 407:
        debug_println("Execute 407 Proxy Authentication Required");
        sipAck();
        sipAuth();
        ret = udp.begin(sdpHeader.udpPortRead);
        debug_println(String("Start UDP read ") + ((ret == 1) ? "TRUE" : "FALSE") + " on Port " + sdpHeader.udpPortRead);
        break;
    case 481:
        debug_println(" Execute 481 Call Leg/Transaction Does Not Exist ");
        break;
    case 486:
        debug_println("Execute 486 Busy Here");
        sock_sip.stop();
        debug_println(String("sock_sip.stop"));
        udp.stop();
        debug_println(String("Stop UDP read "));
        break;
    default:
        debug_println(String(sipHeader.responseCodes) + " does not Exists :-)");
        break;
    }
}
void SipMachine::writeSIPdata(String message)
{
    size_t ret = sock_sip.println(message);
    debugL1_print(String(ret) + " ");
    debugL1_println(message.c_str());
    if (ret < 0)
        debugL1_println(ret);
}

void SipMachine::sipRegister()
{
    int ret = sock_sip.connect(proxyServer.c_str(), port);
    debug_println(String(" Connect SIP Client  on IP ") + proxyServer + " Port " + String(port) + " ret= " + (ret ? "true" : "false"));
    sipHeader.proxyRegistrar = proxyRegistrar;
    sipHeader.via.branch = branch;
    sipHeader.via.port = port;
    sipHeader.via.userClient = userClient;
    sipHeader.from.tagFrom = tagFrom;
    sipHeader.from.telNr = telNr;
    sipHeader.from.userAgent = userAgent;
    sipHeader.to.tagTo = tagTo;
    sipHeader.to.telNr = telNr;
    sipHeader.to.userAgent = userAgent;
    sipHeader.callId.callId = callId + "@" + userAgent;
    sipHeader.contact.telNr = telNr;
    sipHeader.contact.userClient = userClient;
    sipHeader.cSeq.cSeq = 0;
    sipHeader.cSeq.typ = "REGISTER";
    debug_println("*****  Register  *****");
    String str;
    str = "REGISTER sip:";
    str += sipHeader.proxyRegistrar;
    str += " SIP/2.0";
    writeSIPdata(str);
    writeSIPdata(sipHeader.getVia());
    writeSIPdata(sipHeader.getMaxForwards());
    writeSIPdata(sipHeader.getFrom());
    writeSIPdata(sipHeader.getTo());
    writeSIPdata(sipHeader.getCallID());
    writeSIPdata(sipHeader.getCSeq());
    writeSIPdata(sipHeader.getContact());
    writeSIPdata(sipHeader.getAllow());
    writeSIPdata(sipHeader.getContentLength());
    writeSIPdata("");
}

void SipMachine::sipRinging()
{
    debug_println("*****  Ringing *****");
    sipHeader.to.tagTo = randomChr(30);
    sipHeader.contact.telNr = telNr;
    String str;
    str = "SIP/2.0 180 Ringing";
    writeSIPdata(str);
    writeSIPdata(sipHeader.getVia());
    writeSIPdata(sipHeader.getTo());
    writeSIPdata(sipHeader.getFrom());
    writeSIPdata(sipHeader.getCallID());
    writeSIPdata(sipHeader.getCSeq());
    writeSIPdata(sipHeader.getContact());
    writeSIPdata(sipHeader.getContentLength());
    writeSIPdata("");
}

void SipMachine::sipOk()
{
    debug_println("*****  Ok *****");
    sdpHeader.o.sessId = String(sdpHeader.o.sessId.toInt() + 1);
    sdpHeader.o.sessVersion = String(sdpHeader.o.sessVersion.toInt() + 1);
    sdpHeader.o.municastAddress = userClient;
    sdpHeader.c.connectionAddress = userClient;
    sdpHeader.m.port = String(sdpHeader.udpPortRead);
    sdpHeader.m.fmt = String("8 127");
    sipHeader.contact.telNr = telNr;
    sipHeader.contact.userClient = userClient;
    String str;
    str = "SIP/2.0 200 OK";
    writeSIPdata(str);
    writeSIPdata(sipHeader.getVia());
    writeSIPdata(sipHeader.getTo());
    writeSIPdata(sipHeader.getFrom());
    writeSIPdata(sipHeader.getCallID());
    writeSIPdata(sipHeader.getCSeq());
    writeSIPdata(sipHeader.getContact());
    if (sipHeader.cSeq.typ.equals("INVITE"))
    {
        writeSIPdata(sipHeader.getContenType());
        writeSIPdata(sipHeader.getContentLength(sdpHeader.getContent().length() + 2));
        writeSIPdata("");
        writeSIPdata(sdpHeader.getContent());
    }
    else
    {
        writeSIPdata(sipHeader.getContentLength());
        writeSIPdata("");
    }
}

void SipMachine::sipInvite(String telNrTo)
{
    debug_println("*****  Invite *****");
    int ret = sock_sip.connect(proxyServer.c_str(), port);
    debug_println(String(" Connect SIP Client  on IP ") + proxyServer + " Port " + String(port) + " ret= " + (ret ? "true" : "false"));
    sdpHeader.o.sessId = String(random(90000000));
    sdpHeader.o.sessVersion = String(random(90000000));
    sdpHeader.o.municastAddress = userClient;
    sdpHeader.c.connectionAddress = userClient;
    sdpHeader.m.port = String(random(10000, 90000));
    sdpHeader.udpPortRead = sdpHeader.m.port.toInt();
    sdpHeader.m.fmt = String("8 127");
    sipHeader.via.branch = branch;
    sipHeader.via.port = port;
    sipHeader.via.userClient = userClient;
    sipHeader.from.tagFrom = tagFrom;
    sipHeader.from.telNr = telNr;
    sipHeader.from.userAgent = userAgent;
    sipHeader.to.telNr = telNrTo;
    sipHeader.to.userAgent = userAgent;
    sipHeader.to.tagTo = "";
    sipHeader.callId.callId = String(randomChr(7)) + "@" + proxyServer;
    sipHeader.cSeq.cSeq = 1;
    sipHeader.cSeq.typ = "INVITE";
    authType = "INVITE";
    sipHeader.contact.telNr = telNr;
    sipHeader.contact.userClient = userClient;
    String str;
    str = "INVITE sip:";
    str += telNrTo;
    str += "@";
    str += userAgent;
    str += " SIP/2.0";
    writeSIPdata(str);
    writeSIPdata(sipHeader.getVia());
    writeSIPdata(sipHeader.getTo());
    writeSIPdata(sipHeader.getFrom());
    writeSIPdata(sipHeader.getCallID());
    writeSIPdata(sipHeader.getCSeq());
    writeSIPdata(sipHeader.getContact());
    writeSIPdata(sipHeader.getContenType());
    writeSIPdata(sipHeader.getContentLength(sdpHeader.getContent().length() + 2));
    writeSIPdata("");
    writeSIPdata(sdpHeader.getContent());
    status = ringOut;
}

void SipMachine::sipAck()
{
    debug_println("*****  ACK *****");
    sipHeader.cSeq.typ = "ACK";
    String str;
    str = "ACK sip:";
    str += telNrTo;
    str += "@";
    str += userAgent;
    str += " SIP/2.0";
    writeSIPdata(str);
    writeSIPdata(sipHeader.getVia());
    writeSIPdata(sipHeader.getTo());
    writeSIPdata(sipHeader.getFrom());
    writeSIPdata(sipHeader.getCallID());
    writeSIPdata(sipHeader.getCSeq());
    writeSIPdata(sipHeader.getContact());
    writeSIPdata(sipHeader.getContentLength());
    writeSIPdata("");
}

void SipMachine::sipBye()
{
    int ret = sock_sip.connect(proxyServer.c_str(), port);
    debug_println(String(" Connect SIP Client  on IP ") + proxyServer + " Port " + String(port) + " ret= " + (ret ? "true" : "false"));
    debug_println("*****  BYE *****");
    branch = "z9hG4bK-" + randomChr(30);
    sipHeader.via.branch = branch;
    sipHeader.cSeq.typ = "BYE";
    authType = "BYE";
    sipHeader.cSeq.cSeq++;
    sipHeader.from.tagFrom = tagFrom;
    sipHeader.from.telNr = telNr;
    sipHeader.from.userAgent = userAgent;
    sipHeader.to.telNr = telNrTo;
    sipHeader.to.userAgent = userAgent;
    sipHeader.to.tagTo = tagTo;
    String str;
    str = "BYE sip:";
    str += telNrTo;
    str += "@";
    str += userAgent;
    str += " SIP/2.0";
    writeSIPdata(str);
    writeSIPdata(sipHeader.getVia());
    writeSIPdata(sipHeader.getTo());
    writeSIPdata(sipHeader.getFrom());
    writeSIPdata(sipHeader.getCallID());
    writeSIPdata(sipHeader.getCSeq());
    writeSIPdata(sipHeader.getContentLength());
    writeSIPdata("");
    debug_println("*****  BYE *****");
}

void SipMachine::sipAuth()
{
    debug_print("***** ");
    debug_print(authType);
    debug_println("in Auth *****");
    sipHeader.cSeq.cSeq++;
    sipHeader.cSeq.typ = authType;
    sipHeader.authenticate.types = authType;
    if (authType.equals("INVITE"))
    {
        sipHeader.authenticate.telNr = telNrTo;
        sipHeader.authenticate.agent = userAgent;
        sipHeader.to.tagTo = "";
        sipHeader.contact.telNr = telNr;
        sipHeader.contact.userClient = userClient;
    }
    String str;
    str = authType;
    str += " sip:";
    str += telNrTo;
    str += "@";
    str += userAgent;
    str += " SIP/2.0";
    writeSIPdata(str);
    writeSIPdata(sipHeader.getVia());
    writeSIPdata(sipHeader.getTo());
    writeSIPdata(sipHeader.getFrom());
    writeSIPdata(sipHeader.getCallID());
    writeSIPdata(sipHeader.getCSeq());
    writeSIPdata(sipHeader.getContact());
    writeSIPdata(sipHeader.getAuthorisation());
    if (authType.equals("INVITE"))
    {
        writeSIPdata(sipHeader.getContenType());
        writeSIPdata(sipHeader.getContentLength(sdpHeader.getContent().length() + 2));
        writeSIPdata("");
        writeSIPdata(sdpHeader.getContent());
        status = ringOut;
    }
    else
    {
        writeSIPdata(sipHeader.getContentLength());
        writeSIPdata("");
        status = idle;
    }
}
void SipMachine::sipRegisterAuth()
{
    sipHeader.cSeq.cSeq++;
    sipHeader.cSeq.typ = "REGISTER";
    sipHeader.to.tagTo = "";
    sipHeader.authenticate.user = user;
    sipHeader.authenticate.agent = proxyRegistrar;
    sipHeader.authenticate.pwd = pwd;
    sipHeader.authenticate.types = "REGISTER";
    sipHeader.authenticate.telNr = "";
    sipHeader.authenticate.nonceCount = "00000001";
    if (sipHeader.cSeq.cSeq < 3)
    {
        debug_println("*****  Register Auth *****");
        String str;
        str = "REGISTER sip:";
        str += sipHeader.proxyRegistrar;
        str += " SIP/2.0";
        writeSIPdata(str);
        writeSIPdata(sipHeader.getVia());
        writeSIPdata(sipHeader.getMaxForwards());
        writeSIPdata(sipHeader.getTo());
        writeSIPdata(sipHeader.getFrom());
        writeSIPdata(sipHeader.getCallID());
        writeSIPdata(sipHeader.getCSeq());
        writeSIPdata(sipHeader.getContact());
        writeSIPdata(sipHeader.getAllow());
        writeSIPdata(sipHeader.getAuthorisation());
        writeSIPdata(sipHeader.getContentLength());
        writeSIPdata("");
        debugL1_println("*****  Register  Auth out *****");
    }
}

String SipMachine::randomChr(int size)
{
    String ret = "";
    for (int i = 0; i < size; i++)
    {
        if (i % 3 == 0)
        {
            ret += (char)random(48, 57);
        }
        else if (i % 3 == 1)
        {
            ret += (char)random(65, 90);
        }
        else if (i % 3 == 2)
        {
            ret += (char)random(97, 122);
        }
    }
    return ret;
}

String SipMachine::getTelNrIncomingCall()
{
    return sipHeader.from.telNr;
}
void SipMachine::acceptIncomingCall()
{
    sipOk();
    status = callAccept;
}
void SipMachine::makeCall(String telNrTo)
{
    this->telNrTo = telNrTo;
    sipInvite(telNrTo);
}

void SipMachine::bye()
{
    sipBye();
    status = idle;
}
String SipMachine::getKeyPressedLast20()
{
    return dtmf;
}

SipMachine::Status SipMachine::getStatus()
{
    return status;
}

void SipMachine::getDtmfData()
{
    if (udp.available())
    {
        int ret = udp.readBytes((uint8_t *)&(rtpIn.dtmfBuffer), sizeof(rtpIn.dtmfBuffer));
        debugL1_print(String("UDP DTMF read count ") + String(ret));
        debugL1_print(String("          Sequence Number : ") + String(rtpIn.getSequenceNumber()));
        debugL1_print(String(" Timestamp: ") + String(rtpIn.getTimestamp()));
        debugL1_print(String(" event ") + String(rtpIn.dtmfBuffer.event));
        debugL1_print(String(" E ") + String(rtpIn.dtmfBuffer.e));
        debugL1_print(String(" R ") + String(rtpIn.dtmfBuffer.r));
        debugL1_print(String(" volume ") + String(rtpIn.dtmfBuffer.volume));
        debugL1_println(String(" duration ") + String(rtpIn.dtmfBuffer.duration));
        if (rtpIn.dtmfBuffer.e == 1)
        {
            if (dtmf.length() > 20)
                dtmf = dtmf.substring(1);
            String s = String(rtpIn.dtmfBuffer.event, 10);
            if (s.equals("10"))
                s = "*";
            if (s.equals("11"))
                s = "#";
            dtmf += s;
        }
    }
}
void SipMachine::getSpeachData()
{

    if (udp.available())
    {
        int ret = udp.readBytes((uint8_t *)&(rtpIn.rtpBuffer), sizeof(rtpIn.rtpBuffer));
        debugL2_print(String("UDP read count ") + String(ret));
        debugL2_print(String("          Sequence Number : ") + String(rtpIn.getSequenceNumber()));
        debugL2_println(String(" Timestamp: ") + String(rtpIn.getTimestamp()));
        if (((rtpIn.getSequenceNumber() % 5) == 0))
        {
            char dtm = rtpIn.getDtmf();
            if (dtm != ' ')
            {
                if (dtmf.length() > 20)
                    dtmf = dtmf.substring(1);
                dtmf += String(dtm);
            }
        }
    }
}

void SipMachine::writeSpeachData()
{
    int ret = 0;
    ret = udp.beginPacket(udpIpWrite, sdpHeader.udpPortWrite);
    debugL2_print(String("UDP write ") + ((ret == 1) ? "TRUE" : "FALSE"));
    rtpOut.setSequenceNumber(rtpOut.getSequenceNumber() + 1);
    rtpOut.setTimestamp(rtpOut.getTimestamp() + 160);
    ret = udp.write((uint8_t *)&rtpOut.rtpBuffer, sizeof(rtpOut.rtpBuffer));
    debugL2_print(String(" count  ") + String(ret));
    ret = udp.endPacket();
    debugL2_print(String("  ") + ((ret == 1) ? "TRUE " : "FALSE "));
    rtpOut.rtpPos = 0;
    debugL2_print(String(" Sequence Number : ") + String(rtpOut.getSequenceNumber()));
    debugL2_print(String(" Timestamp: ") + String(rtpOut.getTimestamp()));
    debugL2_print(String(" ADDR: ") + udpIpWrite.toString() + " on Port " + (sdpHeader.udpPortWrite));
    debugL2_println("");
}
