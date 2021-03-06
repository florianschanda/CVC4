/*********************                                                        */
/*! \file command_list.h
 ** \verbatim
 ** Top contributors (to current version):
 **   Morgan Deters, Paul Meng
 ** This file is part of the CVC4 project.
 ** Copyright (c) 2009-2017 by the authors listed in the file AUTHORS
 ** in the top-level source directory) and their institutional affiliations.
 ** All rights reserved.  See the file COPYING in the top-level source
 ** directory for licensing information.\endverbatim
 **
 ** \brief A context-sensitive list of Commands, and their cleanup
 **
 ** A context-sensitive list of Commands, and their cleanup.
 **/

#include "cvc4_private.h"

#ifndef __CVC4__SMT__COMMAND_LIST_H
#define __CVC4__SMT__COMMAND_LIST_H

#include "context/cdlist.h"

namespace CVC4 {

class Command;

namespace smt {

struct CommandCleanup {
  void operator()(Command** c);
};/* struct CommandCleanup */

typedef context::CDList<Command*, CommandCleanup> CommandList;

}/* CVC4::smt namespace */
}/* CVC4 namespace */

#endif /* __CVC4__SMT__COMMAND_LIST_H */
