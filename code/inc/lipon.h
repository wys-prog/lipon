#ifndef LIPON_H
#define LIPON_H

#include <stdint.h>
#include <string.h>

struct lipon_CState;

typedef void(*lipon_Function)(struct lipon_CState*);

struct lipon_String {
  const uint8_t* front;
  uint64_t       length;
};

typedef struct lipon_String lipon_String;

struct lipon_CState {
  uint64_t*       stack;
  uint64_t*       heap;
  uint64_t        sp;
  lipon_Function* cfunctions;
  void*           stringpoll;
  uint64_t(*push_string)(void*, const uint8_t*, uint64_t);
  lipon_String(*get_string)(void*, uint64_t);
  void(*remove_string)(void*, uint64_t);
};

typedef struct lipon_CState lipon_CState;

#define LIPON_POP(L)     (L)->stack[--state->sp]
#define LIPON_PUSH(L, V) (L)->stack[state->sp++] = V
#define LIPON_PEEK(L)    (L)->stack[state->sp-1]
#define LIPON_AT(L, I)   (L)->stack[(state->sp-1) - I]
#define LIPON_PUSH_STRING(L, P) { uint64_t REF = (L)->push_string((L)->stringpoll, (const uint8_t*)P, strlen(P)); LIPON_PUSH(L, REF); }
#define LIPON_PUSH_LSTRING(L, P, S) { uint64_t REF = (L)->push_string((L)->stringpoll, (const uint8_t*)P, S); LIPON_PUSH(L, REF); }
#define LIPON_GET_STRING(L, I) (L)->get_string((L)->stringpoll, I);
#define LIPON_REMOVE_STRING(L, I) (L)->remove_string((L)->stringpoll, I); 

#endif // LIPON_H