consider a scenrio, when wifi reamin silent while zigbee transmitt something
as we discussed before, the ofdm symbols(THE TWO LTF) have some empty subcarriers(such as 0-6), 
so now the received data on those subcarriers are soly from zigbee

and for now we could ignore the tof, suppose the zigbee signal reach the receiver at the same time with the wifi signal
since the time we could set it fixed( in other words, the received zigbee singal x(n) is fixed), can we utilize the 6 umpty subcarriers from LTF
to estimate the h, then we could get the channel estimation parameter h = y/x, why we achieve that? because the time is fixed(assumed), so the x is also 
fixed!


For payload "001 HELLO WORLD":

pad byte (0x00)
preamble (0x000000A7) as 4 bytes: 00 00 00 A7
length byte = payload length = 15 = 0x0F
payload ASCII bytes:
0 0 1 H E L L O W O R L D
hex: 30 30 31 20 48 45 4C 4C 4F 20 57 4F 52 4C 44
So final framed bytes are exactly:

00 00 00 00 A7 0F 30 30 31 20 48 45 4C 4C 4F 20 57 4F 52 4C 44


header and preamble only 5 bytes: 
00 00 00 00 A7

each bytes correspond to 128 chips

so 0xa7( 4*128 ==> 4*128 + 127) 512..639