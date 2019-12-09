all:
	cc -g -O2 -Wall -fPIC -dynamiclib -Wl,-undefined,dynamic_lookup -o libTokenBucketRateLimiter.so rateLimiter.c

