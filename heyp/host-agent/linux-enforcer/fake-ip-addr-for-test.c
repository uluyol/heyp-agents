#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static char kJsonOutput[] =
    "[{\"ifindex\":1,\"ifname\":\"lo\",\"flags\":[\"LOOPBACK\",\"UP\",\"LOWER_UP\"],"
    "\"mtu\":65536,\"qdisc\":\"noqueue\",\"operstate\":\"UNKNOWN\",\"group\":\"default\","
    "\"txqlen\":1000,\"link_type\":\"loopback\",\"address\":\"00:00:00:00:00:00\","
    "\"broadcast\":\"00:00:00:00:00:00\",\"addr_info\":[{\"family\":\"inet\",\"local\":"
    "\"127.0.0.1\",\"prefixlen\":8,\"scope\":\"host\",\"label\":\"lo\",\"valid_life_"
    "time\":4294967295,\"preferred_life_time\":4294967295},{\"family\":\"inet6\","
    "\"local\":\"::1\",\"prefixlen\":128,\"scope\":\"host\",\"valid_life_time\":"
    "4294967295,\"preferred_life_time\":4294967295}]},{\"ifindex\":2,\"ifname\":"
    "\"eno49\",\"flags\":[\"BROADCAST\",\"MULTICAST\",\"UP\",\"LOWER_UP\"],\"mtu\":1500,"
    "\"qdisc\":\"mq\",\"operstate\":\"UP\",\"group\":\"default\",\"txqlen\":1000,\"link_"
    "type\":\"ether\",\"address\":\"98:f2:b3:cc:a2:b0\",\"broadcast\":\"ff:ff:ff:ff:ff:"
    "ff\",\"addr_info\":[{\"family\":\"inet\",\"local\":\"128.110.155.6\",\"prefixlen\":"
    "22,\"broadcast\":\"128.110.155.255\",\"scope\":\"global\",\"label\":\"eno49\","
    "\"valid_life_time\":4294967295,\"preferred_life_time\":4294967295},{\"family\":"
    "\"inet6\",\"local\":\"fe80::9af2:b3ff:fecc:a2b0\",\"prefixlen\":64,\"scope\":"
    "\"link\",\"valid_life_time\":4294967295,\"preferred_life_time\":4294967295}]},{"
    "\"ifindex\":3,\"ifname\":\"eno50\",\"flags\":[\"BROADCAST\",\"MULTICAST\",\"UP\","
    "\"LOWER_UP\"],\"mtu\":1500,\"qdisc\":\"mq\",\"operstate\":\"UP\",\"group\":"
    "\"default\",\"txqlen\":1000,\"link_type\":\"ether\",\"address\":\"98:f2:b3:cc:a2:"
    "b1\",\"broadcast\":\"ff:ff:ff:ff:ff:ff\",\"addr_info\":[{\"family\":\"inet\","
    "\"local\":\"192.168.1.2\",\"prefixlen\":24,\"broadcast\":\"192.168.1.255\","
    "\"scope\":\"global\",\"label\":\"eno50\",\"valid_life_time\":4294967295,\"preferred_"
    "life_time\":4294967295},{\"family\":\"inet6\",\"local\":\"fe80::9af2:b3ff:fecc:"
    "a2b1\",\"prefixlen\":64,\"scope\":\"link\",\"valid_life_time\":4294967295,"
    "\"preferred_life_time\":4294967295}]},{\"ifindex\":4,\"ifname\":\"ens1f0\","
    "\"flags\":[\"BROADCAST\",\"MULTICAST\"],\"mtu\":1500,\"qdisc\":\"mq\",\"operstate\":"
    "\"DOWN\",\"group\":\"default\",\"txqlen\":1000,\"link_type\":\"ether\",\"address\":"
    "\"9c:dc:71:5d:12:a0\",\"broadcast\":\"ff:ff:ff:ff:ff:ff\",\"addr_info\":[]},{"
    "\"ifindex\":5,\"ifname\":\"ens1f1\",\"flags\":[\"BROADCAST\",\"MULTICAST\"],\"mtu\":"
    "1500,\"qdisc\":\"mq\",\"operstate\":\"DOWN\",\"group\":\"default\",\"txqlen\":1000,"
    "\"link_type\":\"ether\",\"address\":\"9c:dc:71:5d:12:a1\",\"broadcast\":\"ff:ff:ff:"
    "ff:ff:ff\",\"addr_info\":[]}]";

static char kOnelineOutput[] =
    "1: lo    inet 127.0.0.1/8 scope host lo\\       valid_lft forever preferred_lft "
    "forever\n"
    "2: eth0    inet 169.254.0.1/30 brd 169.254.0.3 scope global eth0\\       valid_lft "
    "forever preferred_lft forever";

int main(int argc, char** argv) {
  bool is_json = false;
  bool is_oneline = false;
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "-json") == 0) {
      is_json = true;
    } else if (strcmp(argv[i], "-oneline") == 0) {
      is_oneline = true;
    }
  }
  if (is_json && is_oneline) {
    fprintf(stderr, "cannot pass both -json and -oneline\n");
  }
  if (is_json) {
    puts(kJsonOutput);
  }
  if (is_oneline) {
    puts(kOnelineOutput);
  }
}
