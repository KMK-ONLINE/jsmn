#include <stdlib.h>
#include <string.h>

#include "jsmn.h"

/**
 * Allocates a fresh unused token from the token pull.
 */
static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser,
		jsmntok_t *tokens, size_t num_tokens) {
	jsmntok_t *tok;
	if (parser->toknext >= num_tokens) {
		return NULL;
	}
	tok = &tokens[parser->toknext++];
	tok->start = tok->end = -1;
	tok->size = 0;
#ifdef JSMN_PARENT_LINKS
	tok->parent = -1;
#endif
	return tok;
}

/**
 * Fills token type and boundaries.
 */
static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type,
                            int start, int end) {
	token->type = type;
	token->start = start;
	token->end = end;
	token->size = 0;
}

/**
 * Fills next available token with JSON primitive.
 */
static int jsmn_parse_primitive(jsmn_parser *parser, const char *js,
		size_t len, jsmntok_t *tokens, size_t num_tokens) {
	jsmntok_t *token;
	int start;

	start = parser->pos;

	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		switch (js[parser->pos]) {
#ifndef JSMN_STRICT
			/* In strict mode primitive must be followed by "," or "}" or "]" */
			case ':':
#endif
			case '\t' : case '\r' : case '\n' : case ' ' :
			case ','  : case ']'  : case '}' :
				goto found;
		}
		if (js[parser->pos] < 32 || js[parser->pos] >= 127) {
			parser->pos = start;
			return JSMN_ERROR_INVAL;
		}
	}
#ifdef JSMN_STRICT
	/* In strict mode primitive must be followed by a comma/object/array */
	parser->pos = start;
	return JSMN_ERROR_PART;
#endif

found:
	if (tokens == NULL) {
		parser->pos--;
		return 0;
	}
	token = jsmn_alloc_token(parser, tokens, num_tokens);
	if (token == NULL) {
		parser->pos = start;
		return JSMN_ERROR_NOMEM;
	}
	jsmn_fill_token(token, JSMN_PRIMITIVE, start, parser->pos);
#ifdef JSMN_PARENT_LINKS
	token->parent = parser->toksuper;
#endif
	parser->pos--;
	return 0;
}

/**
 * Filsl next token with JSON string.
 */
static int jsmn_parse_string(jsmn_parser *parser, const char *js,
		size_t len, jsmntok_t *tokens, size_t num_tokens) {
	jsmntok_t *token;

	int start = parser->pos;

	parser->pos++;

	/* Skip starting quote */
	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		char c = js[parser->pos];

		/* Quote: end of string */
		if (c == '\"') {
			if (tokens == NULL) {
				return 0;
			}
			token = jsmn_alloc_token(parser, tokens, num_tokens);
			if (token == NULL) {
				parser->pos = start;
				return JSMN_ERROR_NOMEM;
			}
			jsmn_fill_token(token, JSMN_STRING, start+1, parser->pos);
#ifdef JSMN_PARENT_LINKS
			token->parent = parser->toksuper;
#endif
			return 0;
		}

		/* Backslash: Quoted symbol expected */
		if (c == '\\' && parser->pos + 1 < len) {
			int i;
			parser->pos++;
			switch (js[parser->pos]) {
				/* Allowed escaped symbols */
				case '\"': case '/' : case '\\' : case 'b' :
				case 'f' : case 'r' : case 'n'  : case 't' :
					break;
				/* Allows escaped symbol \uXXXX */
				case 'u':
					parser->pos++;
					for(i = 0; i < 4 && parser->pos < len && js[parser->pos] != '\0'; i++) {
						/* If it isn't a hex character we have an error */
						if(!((js[parser->pos] >= 48 && js[parser->pos] <= 57) || /* 0-9 */
									(js[parser->pos] >= 65 && js[parser->pos] <= 70) || /* A-F */
									(js[parser->pos] >= 97 && js[parser->pos] <= 102))) { /* a-f */
							parser->pos = start;
							return JSMN_ERROR_INVAL;
						}
						parser->pos++;
					}
					parser->pos--;
					break;
				/* Unexpected symbol */
				default:
					parser->pos = start;
					return JSMN_ERROR_INVAL;
			}
		}
	}
	parser->pos = start;
	return JSMN_ERROR_PART;
}

/**
 * Parse JSON string and fill tokens.
 */
int jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
		jsmntok_t *tokens, unsigned int num_tokens) {
	int r;
	int i;
	jsmntok_t *token;
	int count = 0;

	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		char c;
		jsmntype_t type;

		c = js[parser->pos];
		switch (c) {
			case '{': case '[':
				count++;
				if (tokens == NULL) {
					break;
				}
				token = jsmn_alloc_token(parser, tokens, num_tokens);
				if (token == NULL)
					return JSMN_ERROR_NOMEM;
				if (parser->toksuper != -1) {
					tokens[parser->toksuper].size++;
#ifdef JSMN_PARENT_LINKS
					token->parent = parser->toksuper;
#endif
				}
				token->type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
				token->start = parser->pos;
				parser->toksuper = parser->toknext - 1;
				break;
			case '}': case ']':
				if (tokens == NULL)
					break;
				type = (c == '}' ? JSMN_OBJECT : JSMN_ARRAY);
#ifdef JSMN_PARENT_LINKS
				if (parser->toknext < 1) {
					return JSMN_ERROR_INVAL;
				}
				token = &tokens[parser->toknext - 1];
				for (;;) {
					if (token->start != -1 && token->end == -1) {
						if (token->type != type) {
							return JSMN_ERROR_INVAL;
						}
						token->end = parser->pos + 1;
						parser->toksuper = token->parent;
						break;
					}
					if (token->parent == -1) {
						break;
					}
					token = &tokens[token->parent];
				}
#else
				for (i = parser->toknext - 1; i >= 0; i--) {
					token = &tokens[i];
					if (token->start != -1 && token->end == -1) {
						if (token->type != type) {
							return JSMN_ERROR_INVAL;
						}
						parser->toksuper = -1;
						token->end = parser->pos + 1;
						break;
					}
				}
				/* Error if unmatched closing bracket */
				if (i == -1) return JSMN_ERROR_INVAL;
				for (; i >= 0; i--) {
					token = &tokens[i];
					if (token->start != -1 && token->end == -1) {
						parser->toksuper = i;
						break;
					}
				}
#endif
				break;
			case '\"':
				r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
				if (r < 0) return r;
				count++;
				if (parser->toksuper != -1 && tokens != NULL)
					tokens[parser->toksuper].size++;
				break;
			case '\t' : case '\r' : case '\n' : case ' ':
				break;
			case ':':
				parser->toksuper = parser->toknext - 1;
				break;
			case ',':
				if (tokens != NULL &&
						tokens[parser->toksuper].type != JSMN_ARRAY &&
						tokens[parser->toksuper].type != JSMN_OBJECT) {
#ifdef JSMN_PARENT_LINKS
					parser->toksuper = tokens[parser->toksuper].parent;
#else
					for (i = parser->toknext - 1; i >= 0; i--) {
						if (tokens[i].type == JSMN_ARRAY || tokens[i].type == JSMN_OBJECT) {
							if (tokens[i].start != -1 && tokens[i].end == -1) {
								parser->toksuper = i;
								break;
							}
						}
					}
#endif
				}
				break;
#ifdef JSMN_STRICT
			/* In strict mode primitives are: numbers and booleans */
			case '-': case '0': case '1' : case '2': case '3' : case '4':
			case '5': case '6': case '7' : case '8': case '9':
			case 't': case 'f': case 'n' :
				/* And they must not be keys of the object */
				if (tokens != NULL) {
					jsmntok_t *t = &tokens[parser->toksuper];
					if (t->type == JSMN_OBJECT ||
							(t->type == JSMN_STRING && t->size != 0)) {
						return JSMN_ERROR_INVAL;
					}
				}
#else
			/* In non-strict mode every unquoted value is a primitive */
			default:
#endif
				r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
				if (r < 0) return r;
				count++;
				if (parser->toksuper != -1 && tokens != NULL)
					tokens[parser->toksuper].size++;
				break;

#ifdef JSMN_STRICT
			/* Unexpected char in strict mode */
			default:
				return JSMN_ERROR_INVAL;
#endif
		}
	}

	if (!tokens)
		return count;

	for (i = parser->toknext - 1; i >= 0; i--) {
		/* Unmatched opened object or array */
		if (tokens[i].start != -1 && tokens[i].end == -1) {
			return JSMN_ERROR_PART;
		}
	}

	return count;
}

/**
 * Creates a new parser based over a given  buffer with an array of tokens
 * available.
 */
void jsmn_init(jsmn_parser *parser) {
	parser->pos = 0;
	parser->toknext = 0;
	parser->toksuper = -1;
}


struct sized_str {
	char* val;   /* pointer to string */
	size_t size; /* remaning size of string buffer (including nul) */
};


/**
 * Append min(src_len,dst->size) of src to dst->val, both advancing
 * dst->val and decrementing dst->size by the number of bytes appended.
 *
 * This function null-terminate the string unless dst->size is 0.
 */
static void
str_append(
	struct sized_str* dst, /* destination and size of string buffer */
	const char* src,       /* source string buffer */
	size_t src_len         /* length of source (excluding nul) */
) {
	if (dst->size <= src_len) {
		if (dst->size < 1) {
			return; /* no hope, give up */
		}
		/* truncate to fit (and maintain nul termination) */
		src_len = dst->size - 1;
	}
	memcpy(dst->val, src, src_len);
	dst->size -= src_len;
	dst->val += src_len;
	dst->val[0] = '\0';
}

/* Build a string representing JSON represented by tokens, appending
 * the output to *out, and advancing *out to the end of the string.
 *
 * @returns number of tokens consumed
 */
static int jsmn_stringify(
	struct sized_str *out, /* partially built output string */
	jsmntok_t *tokens,     /* token stream */
	int num_tokens,        /* remaining tokens in token stream */
	const char* js         /* input json (indexed by tokens) */
) {
	if (num_tokens < 1) {
		return 0;
	}

	switch (tokens->type) {
	case JSMN_PRIMITIVE: { /* simple token, append to output */
		int token_len = tokens->end - tokens->start;
		str_append(out, js + tokens->start, token_len);
		return 1;
	}

	case JSMN_STRING: { /* simple token, append to output */
		int token_len = tokens->end - tokens->start;
		str_append(out, "\"", 1);
		str_append(out, js + tokens->start, token_len);
		str_append(out, "\"", 1);
		return 1;
	}

	case JSMN_ARRAY: {
		int j, t = 1; /* t = token index */
		str_append(out, "[", 1);
		for (j = 0; j < tokens->size; j++) { /* for each value in list */
			if (j) /* not first value in list */
				str_append(out, ",", 1);
			t += jsmn_stringify(out, tokens + t, num_tokens - t, js); /* value */
		}
		str_append(out, "]", 1);
		return t;
	}

	case JSMN_OBJECT: {
		int j, t = 1; /* t = token index */
		str_append(out, "{", 1);
		for (j = 0; j < tokens->size; j++) { /* for each pair in map */
			if (t >= num_tokens)
				break;
#ifdef JSMN_STRICT
			if (tokens[t].type != JSMN_STRING) {
				/* error, map must be string : value */
				return t;
			}
#endif
			if (j) /* not first pair in map */
				str_append(out, ",", 1);
			t += jsmn_stringify(out, tokens + t, num_tokens - t, js); /* string */

			str_append(out, ":", 1);
			t += jsmn_stringify(out, tokens + t, num_tokens - t, js); /* value */
		}
		str_append(out, "}", 1);
		return t;
	}
	}

	/* unreachable */
	return 0;
}


/**
 * Convert json blob represented by js of length js_len and previously
 * parsed into tokens , to a minified representation that omits all
 * redundant white space.
 *
 * @returns NULL on error, otherwise,
 *          a malloc'd string of serialised data; owned by the caller
 */
char * jsmn_compact(jsmntok_t *tokens, unsigned int num_tokens, int start_token, const char * js, size_t js_len)
{
	/* if js_len js is covered by [start_token, start_token + num_tokens),
	 * then, by definition the output will be <= js_len (+1 for null) */
	char* retval;
	struct sized_str str = {
		/* .val */  retval = malloc(js_len + 1),
		/* .size */ js_len + 1
	};
	if (!retval)
		return NULL;

	jsmn_stringify(&str, tokens + start_token, num_tokens, js);
	return retval;
}

/**
 * Unescape the JSON string that contains escaped characters
 * NOTE: JSON encodes special characters such as double quotes,
 * back/forward slashes and controlling characters with escapes.
 * This function restores the escaped string back to normal
 * string for further parsing if needed
 */
void jsmn_unescape_string(char * js)
{
	int pos;
	size_t js_len = strlen(js);
	for (pos = 0; pos < js_len; pos++) {
		char c = js[pos];
		int move = 0;
		if (c == '\\' && pos + 1 < js_len) {
			switch (js[pos + 1]) {
				/* escaped quotes */
				case '\"':
				case '/' :
				case '\\':
					move = 1;
					break;
				/* Allowed escaped symbols */
				case 'b' :
					js[pos + 1] = '\b';
					move = 1;
					break;
				case 'f' :
					js[pos + 1] = '\f';
					move = 1;
					break;
				case 'r' :
					js[pos + 1] = '\r';
					move = 1;
					break;
				case 'n' :
					js[pos + 1] = '\n';
					move = 1;
					break;
				case 't' :
					js[pos + 1] = '\t';
					move = 1;
					break;
				/* ignoring escaped unicode symbol \uXXXX */
				case 'u':
				default:
					break;
			}
			if (move) {
				// including terminating NUL
				memmove(&js[pos], &js[pos + 1], js_len - pos);
				js_len--;
			}
		}
	}
}

