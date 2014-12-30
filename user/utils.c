#include "osapi.h"
#include "mem.h"
#include "user_interface.h"
#include "noolite_platform.h"
#include "utils.h"

// You must free the result if result is non-NULL.
char ICACHE_FLASH_ATTR *str_replace(const char *orig, char *rep, char *with) {
    char *result; // the return string
    char *ins;    // the next insert point
    char *tmp;    // varies
    int len_rep;  // length of rep
    int len_with; // length of with
    int len_front; // distance between rep and end of last rep
    int count;    // number of replacements

    if (!orig)
        return NULL;
    if (!rep)
        rep = "";
    len_rep = os_strlen(rep);
    if (!with)
        with = "";
    len_with = os_strlen(with);

    ins = (char*)orig;
    for (count = 0; tmp = (char *)os_strstr(ins, rep); ++count) {
        ins = tmp + len_rep;
    }

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    tmp = result = (char *)os_malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    while (count--) {
        ins = (char *)os_strstr(orig, rep);
        len_front = ins - orig;
        tmp = (char *)os_strncpy(tmp, orig, len_front) + len_front;
        tmp = (char *)os_strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    os_strcpy(tmp, orig);
    return result;
}

void ICACHE_FLASH_ATTR bin2strhex(unsigned char *bin, unsigned int binsz, char **result)
{
	char hex_str[]= "0123456789abcdef";
	unsigned int i;
	*result = (char *)os_malloc(binsz*2+1);
	(*result)[binsz*2] = 0;
	if (!binsz)
		return;
	for (i = 0; i < binsz; i++)
	{
		(*result)[i*2+0] = hex_str[(bin[i] >> 4) & 0x0F];
		(*result)[i*2+1] = hex_str[(bin[i]) & 0x0F];
	}
}

