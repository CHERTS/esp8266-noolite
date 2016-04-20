#ifndef __UTILS_H_
#define __UTILS_H_

typedef struct HttpdPriv HttpdPriv;
typedef struct HttpdConnData HttpdConnData;

//A struct describing a http connection. This gets passed to cgi functions.
struct HttpdConnData {
	struct espconn *conn;
	HttpdPriv *priv;
	int postLen;
};

char *str_replace(const char *orig, char *rep, char *with);
void bin2strhex(unsigned char *bin, unsigned int binsz, char **result);

#define isItNum(str,v) ({char *s = (char *)(str), *e; (v) = 0;  \
      isdigit(*s)? (v) = strtol(s,&e,10), !*e: 0;})

#define IsItNum(str,v,chrs) ({char *s = (char *)(str),  \
        *c = (char *)(chrs), *e;                        \
      (v) = strtol(s,&e,10);                            \
      e == s? 0:                                        \
        *e == 0? 1:                                     \
        (c && *c)? strchr(c, *e)? 1: 0                  \
        :0; })

#define IsItNumEptr(str,ptr,v,chrs) ({char *s = (char *)(str), \
        *c = (char *)(chrs), *e;                               \
      (v) = strtol(s,&e,10); (ptr) = e;                        \
      e == s? 0:                                               \
        *e == 0? 1:                                            \
        (c && *c)? strchr(c, *e)? 1: 0                         \
        :0; })

#endif /* __UTILS_H_ */
