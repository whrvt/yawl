CC := clang
CFLAGS := -Wall -Werror -Wextra -Wpedantic -std=gnu23 -march=x86-64 -mtune=generic -Oz -fuse-ld=lld -ffunction-sections -fdata-sections -Wl,--gc-sections,-s $(CFLAGS)

ifeq ($(CC),gcc)
	CFLAGS += -fno-lto
endif

all: yawl

yawl: yawl.c
	$(CC) yawl.c -o yawl $(CFLAGS) -Ideps/include -DCURL_STATICLIB -Ldeps/lib -lcurl -lcares -lpthread -lnghttp3 -lnghttp2 -lidn2 -lunistring -lssh2 -lpsl -lssl -lcrypto -ldl -pthread -lzstd -lbrotlidec -lbrotlicommon -lz -larchive -llzma -lacl -static

clean: 
	rm -rf yawl
