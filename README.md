# Traceroute
An implementation of traceroute in C
---

# Important notes
1. This suppports a 2-3 flags to change parameters but does not support all the flags that traceroute has in a linux distribution. 
2. **The Makefile does call sudo** that is for setting the "s" bit so that the program can create a raw socket
