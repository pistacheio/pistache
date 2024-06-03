/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Preprocessor macro for enclosing something in quotes

#ifndef _PIST_QUOTE_H_
#define _PIST_QUOTE_H_

/* ------------------------------------------------------------------------- */

#ifndef PIST_QUOTE // used for placing quote signs around a preprocessor parm
  #define PIST_Q(x) #x
  #define PIST_QUOTE(x) PIST_Q(x)
#endif

/* ------------------------------------------------------------------------- */

#endif // ifdef _PIST_QUOTE_H_
