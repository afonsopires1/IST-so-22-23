#ifndef _BETTER_ASSERT_H
#define _BETTER_ASSERT_H

#include <stdbool.h>
#include <stdio.h>

#define ALWAYS_ASSERT(CONDEXPR, MSG)                                           \
    {                                                                          \
        bool should_quit = !(CONDEXPR);                                        \
        if (should_quit)                                                       \
            PANIC(MSG);                                                        \
    }

#define PANIC(MSG)                                                             \
    {                                                                          \
        fprintf(stderr, "Aborting. Reason: %s\n", (MSG));                      \
        abort();                                                               \
    }

#endif


/* *betterassert.h*
Define macros para usar nos outros programas. Serve para verificar condições e atuar caso estas não sejam cumpridas.

ALWAYS_ASSERT(CONDEXPR, MSG) e PANIC(MSG):
Recebe dois argumentos: uma expressão de condição('CONDEXPR') e uma mensagem('MSG'). 
Se a condição for avaliada como falsa, chama o PANIC com a mensagem específica. Imprime a mensagem para o 'stderr' e chama o 'abort' que termina o programa. 

'#ifndef' e '#define' são para prevenir o header file de ser incluído múltiplas vezes no mesmo programa. */