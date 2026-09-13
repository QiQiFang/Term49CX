#ifndef TERM49CX_SYSTEM_PROMPT_H
#define TERM49CX_SYSTEM_PROMPT_H

#ifdef __cplusplus
extern "C" {
#endif

/* The returned text is malloc-owned by the caller. A nonpositive result
 * never sends text to the terminal. */
int system_prompt_init(int *argc, char **argv);
int system_prompt_run(char **utf8_text);

#ifdef __cplusplus
}
#endif

#endif
