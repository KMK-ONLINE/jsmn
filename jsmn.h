#ifndef __JSMN_H_
#define __JSMN_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JSMN_PARENT_LINKS

/**
 * JSON type identifier. Basic types are:
 * 	o Object
 * 	o Array
 * 	o String
 * 	o Other primitive: number, boolean (true/false) or null
 */
typedef enum {
	JSMN_PRIMITIVE = 0,
	JSMN_OBJECT = 1,
	JSMN_ARRAY = 2,
	JSMN_STRING = 3
} jsmntype_t;

/* Not enough tokens were provided */
#define JSMN_ERROR_NOMEM -1
/* Invalid character inside JSON string */
#define JSMN_ERROR_INVAL -2
/* The string is not a full JSON packet, more bytes expected */
#define JSMN_ERROR_PART -3

/**
 * JSON token description.
 * @param		type	type (object, array, string etc.)
 * @param		start	start position in JSON data string
 * @param		end		end position in JSON data string
 */
typedef struct {
	jsmntype_t type;
	int start;
	int end;
	int size;
#ifdef JSMN_PARENT_LINKS
	int parent;
#endif
} jsmntok_t;

/**
 * JSON parser. Contains an array of token blocks available. Also stores
 * the string being parsed now and current position in that string
 */
typedef struct {
	unsigned int pos; /* offset in the JSON string */
	unsigned int toknext; /* next token to allocate */
	int toksuper; /* superior token node, e.g parent object or array */
} jsmn_parser;

/**
 * Create JSON parser over an array of tokens
 */
void jsmn_init(jsmn_parser *parser);

/**
 * Run JSON parser. It parses a JSON data string into and array of tokens, each describing
 * a single JSON object.
 */
int jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
		jsmntok_t *tokens, unsigned int num_tokens);

/**
 * From tokens to compact JSON format that all unnecessary spaces are stripped
 * for storage
 * @start_token the token where the data starts to encode
 * @returns a string if succeeded and it is the caller's responsibility to free
 */
char * jsmn_compact(jsmntok_t *tokens, unsigned int num_tokens, int start_token, const char * js, size_t len);

/**
 * Unescape the JSON string that contains escaped characters
 * NOTE: JSON encodes special characters such as double quotes,
 * back/forward slashes and controlling characters with escapes.
 * This function restores the escaped string back to normal
 * string for further parsing if needed
 * @param js the json string that may have escaped characters,
 *           must be NUL terminated
 */
void jsmn_unescape_string(char * js);
#ifdef __cplusplus
}
#endif

#endif /* __JSMN_H_ */
