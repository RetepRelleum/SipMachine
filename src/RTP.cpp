#include "RTP.h"

RTP::RTP()
{
    is_big = is_big_endian();
    rtpBuffer.version = 2;
    rtpBuffer.p = 0;
    rtpBuffer.x = 0;
    rtpBuffer.cc = 0;
    rtpBuffer.m = 0;
    rtpBuffer.pt = 8;
    setSSRCIdentifier(random(10000000));
    setSequenceNumber(random(1000));
    setTimestamp(random(1000));
}
bool RTP::is_big_endian(void)
{
    union
    {
        uint32_t i;
        char c[4];
    } bint = {0x01020304};

    return bint.c[0] == 1;
}
void RTP::put(int16_t value)
{

    size_t p = (micros() % 20000) / 125;
    rtpBuffer.b[p] = ALaw_Encode(value);
 
}
int16_t RTP::get(uint8_t pos)
{
    switch (rtpBuffer.pt)
    {
    case 0:
        return MuLaw_Decode(rtpBuffer.b[pos]);
        break;
    case 8:
        return ALaw_Decode(rtpBuffer.b[pos]);
        break;
    default:
        return 0;
        break;
    }
}
int8_t RTP::ALaw_Encode(int16_t number)
{
    const uint16_t ALAW_MAX = 0xFFF;
    uint16_t mask = 0x800;
    uint8_t sign = 0;
    uint8_t position = 11;
    uint8_t lsb = 0;
    if (number < 0)
    {
        number = -number;
        sign = 0x80;
    }
    if (number > ALAW_MAX)
    {
        number = ALAW_MAX;
    }
    for (; ((number & mask) != mask && position >= 5); mask >>= 1, position--)
        ;
    lsb = (number >> ((position == 4) ? (1) : (position - 4))) & 0x0f;
    return (sign | ((position - 4) << 4) | lsb) ^ 0x55;
}
int16_t RTP::ALaw_Decode(int8_t number)
{
    uint8_t sign = 0x00;
    uint8_t position = 0;
    int16_t decoded = 0;
    number ^= 0x55;
    if (number & 0x80)
    {
        number &= ~(1 << 7);
        sign = -1;
    }
    position = ((number & 0xF0) >> 4) + 4;
    if (position != 4)
    {
        decoded = ((1 << position) | ((number & 0x0F) << (position - 4)) | (1 << (position - 5)));
    }
    else
    {
        decoded = (number << 1) | 1;
    }
    return (sign == 0) ? (decoded) : (-decoded);
}
int8_t RTP::MuLaw_Encode(int16_t number)
{
    const uint16_t MULAW_MAX = 0x1FFF;
    const uint16_t MULAW_BIAS = 33;
    uint16_t mask = 0x1000;
    uint8_t sign = 0;
    uint8_t position = 12;
    uint8_t lsb = 0;
    if (number < 0)
    {
        number = -number;
        sign = 0x80;
    }
    number += MULAW_BIAS;
    if (number > MULAW_MAX)
    {
        number = MULAW_MAX;
    }
    for (; ((number & mask) != mask && position >= 5); mask >>= 1, position--)
        ;
    lsb = (number >> (position - 4)) & 0x0f;
    return (~(sign | ((position - 5) << 4) | lsb));
}
int16_t RTP::MuLaw_Decode(int8_t number)
{
    const uint16_t MULAW_BIAS = 33;
    uint8_t sign = 0, position = 0;
    int16_t decoded = 0;
    number = ~number;
    if (number & 0x80)
    {
        number &= ~(1 << 7);
        sign = -1;
    }
    position = ((number & 0xF0) >> 4) + 5;
    decoded = ((1 << position) | ((number & 0x0F) << (position - 4)) | (1 << (position - 5))) - MULAW_BIAS;
    return (sign == 0) ? (decoded) : (-(decoded));
}
void RTP::fft(CArray &x)
{
    const size_t N = x.size();
    if (N <= 1)
        return;

    // divide
    CArray even = x[std::slice(0, N / 2, 2)];
    CArray odd = x[std::slice(1, N / 2, 2)];

    // conquer
    fft(even);
    fft(odd);

    // combine
    for (size_t k = 0; k < N / 2; ++k)
    {
        Complex t = std::polar(1.0, -2 * PI * k / N) * odd[k];
        x[k] = even[k] + t;
        x[k + N / 2] = even[k] - t;
    }
}
char RTP::getDtmf()
{
    CArray data(128);
    for (uint8_t i = 0; i < 128; i++)
        data[i] = get(i);

    fft(data);
    double b[] = {(abs(data[19].real()) + abs(data[11].real())),
                  abs(data[21].real()) + abs(data[11].real()),
                  abs(data[24].real()) + abs(data[11].real()),
                  abs(data[19].real()) + abs(data[12].real()),
                  abs(data[21].real()) + abs(data[12].real()),
                  abs(data[24].real()) + abs(data[12].real()),
                  abs(data[19].real()) + abs(data[14].real()),
                  abs(data[21].real()) + abs(data[14].real()),
                  abs(data[24].real()) + abs(data[14].real()),
                  abs(data[19].real()) + abs(data[15].real()),
                  abs(data[21].real()) + abs(data[15].real()),
                  abs(data[24].real()) + abs(data[15].real())};

    char l[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'};

    int8_t index = 0;
    double max = 0;

    for (uint8_t i = 0; i < 12; i++)
    {
        if (b[i] > max)
        {
            max = b[i];
            index = i;
        };
    }
    if (max > 50000)
        return l[index];
    else
        return ' ';
}
