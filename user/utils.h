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

#endif /* __UTILS_H_ */
