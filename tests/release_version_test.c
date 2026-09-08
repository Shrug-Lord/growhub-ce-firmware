#define GROWHUB_VERSION "1.2.0C"
#include "../firmware/src/release_version.h"
#include <assert.h>
int main(void) {
 unsigned parts[3];
 const char *bad[]={"", "v1.2.0", "v1.2.0C-beta", "v01.2.0C", "v1.2.0C/evil", "main", "v9999999999.2.0C", "v1.2", "v-1.2.0C"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(!release_version_parse(bad[i],parts));
 assert(release_version_parse("v1.2.0C",parts));
 assert(!release_version_newer("v1.2.0C")); assert(!release_version_newer("v1.1.9C"));
 assert(release_version_newer("v1.2.1C")); assert(release_version_newer("v1.10.0C")); assert(release_version_newer("v2.0.0C"));
 return 0;
}
