/*********************                                                        */
/*! \file cegis_unif.h
 ** \verbatim
 ** Top contributors (to current version):
 **   Haniel Barbosa, Andrew Reynolds
 ** This file is part of the CVC4 project.
 ** Copyright (c) 2009-2018 by the authors listed in the file AUTHORS
 ** in the top-level source directory) and their institutional affiliations.
 ** All rights reserved.  See the file COPYING in the top-level source
 ** directory for licensing information.\endverbatim
 **
 ** \brief cegis with unification techinques
 **/
#include "cvc4_private.h"

#ifndef __CVC4__THEORY__QUANTIFIERS__SYGUS__CEGIS_UNIF_H
#define __CVC4__THEORY__QUANTIFIERS__SYGUS__CEGIS_UNIF_H

#include <map>
#include <vector>

#include "theory/quantifiers/sygus/cegis.h"
#include "theory/quantifiers/sygus/sygus_unif_rl.h"

namespace CVC4 {
namespace theory {
namespace quantifiers {

/** Cegis Unif Enumeration Manager
 *
 * This class enforces a decision heuristic that limits the number of
 * unique values given to the set of heads of evaluation points, which are
 * variables of sygus datatype type that are introduced by CegisUnif.
 *
 * It maintains a set of guards, call them G_uq_1 ... G_uq_n, where the
 * semantics of G_uq_i is "for each type, the heads of evaluation points of that
 * type are interpreted as a value in a set whose cardinality is at most i".
 *
 * To enforce this, we introduce sygus enumerator(s) of the same type as the
 * heads of evaluation points registered to this class and add lemmas that
 * enforce that these terms are equal to at least one enumerator (see
 * registerEvalPtAtValue).
 */
class CegisUnifEnumManager
{
 public:
  CegisUnifEnumManager(QuantifiersEngine* qe, CegConjecture* parent);
  /** initialize candidates
   *
   * Notify this class that it will be managing enumerators for the vector
   * of functions-to-synthesize (candidate variables) in candidates. This
   * function should only be called once.
   *
   * Each candidate c in cs should be such that we are using a
   * synthesis-by-unification approach for c.
   */
  void initialize(const std::vector<Node>& cs,
                  const std::map<Node, std::vector<Node>>& strategy_lemmas);
  /** register evaluation point for candidate
   *
   * This notifies this class that eis is a set of heads of evaluation points
   * for candidate c, where c should be a candidate that was passed to
   * initialize in the vector cs.
   *
   * This may add new lemmas of the form described above
   * registerEvalPtAtValue on the output channel of d_qe.
   */
  void registerEvalPts(const std::vector<Node>& eis, Node c);
  /** get next decision request
   *
   * This function has the same contract as Theory::getNextDecisionRequest.
   *
   * If no guard G_uq_* is asserted positively, then this method returns the
   * minimal G_uq_i that is not asserted negatively. It allocates this guard
   * if necessary.
   *
   * This call may add new lemmas of the form described above
   * registerEvalPtAtValue on the output channel of d_qe.
   */
  Node getNextDecisionRequest(unsigned& priority);

 private:
  /** reference to quantifier engine */
  QuantifiersEngine* d_qe;
  /** sygus term database of d_qe */
  TermDbSygus* d_tds;
  /** reference to the parent conjecture */
  CegConjecture* d_parent;
  /** whether this module has been initialized */
  bool d_initialized;
  /** null node */
  Node d_null;
  /** information per initialized type */
  class TypeInfo
  {
   public:
    TypeInfo() {}
    /** candidates for this type */
    std::vector<Node> d_candidates;
    /** the set of enumerators we have allocated for this candidate */
    std::vector<Node> d_enums;
    /** the set of evaluation points of this type */
    std::vector<Node> d_eval_points;
    /** symmetry breaking lemma template for this type */
    Node d_sbt_lemma;
    /** argument (to be instantiated) of symmetry breaking lemma template */
    Node d_sbt_arg;
  };
  /** map types to the above info */
  std::map<TypeNode, TypeInfo> d_ce_info;
  /** literals of the form G_uq_n for each n */
  std::map<unsigned, Node> d_guq_lit;
  /** Have we returned a decision in the current SAT context? */
  context::CDO<bool> d_ret_dec;
  /**
   * The minimal n such that G_uq_n is not asserted negatively in the
   * current SAT context.
   */
  context::CDO<unsigned> d_curr_guq_val;
  /** increment the number of enumerators */
  void incrementNumEnumerators();
  /**
   * Get the "current" literal G_uq_n, where n is the minimal n such that G_uq_n
   * is not asserted negatively in the current SAT context.
   */
  Node getCurrentLiteral() const;
  /** get literal G_uq_n */
  Node getLiteral(unsigned n) const;
  /** register evaluation point at size
   *
   * This sends a lemma of the form:
   *   G_uq_n => ei = d1 V ... V ei = dn
   * on the output channel of d_qe, where d1...dn are sygus enumerators of the
   * same type (ct) as ei.
   */
  void registerEvalPtAtSize(TypeNode ct, Node ei, Node guq_lit, unsigned n);
};

/** Synthesizes functions in a data-driven SyGuS approach
 *
 * Data is derived from refinement lemmas generated through the regular CEGIS
 * approach. SyGuS is used to generate terms for classifying the data
 * (e.g. using decision tree learning) and thus generate a candidates for
 * functions-to-synthesize.
 *
 * This approach is inspired by the divide and conquer synthesis through
 * unification approach by Alur et al. TACAS 2017, by ICE-based invariant
 * synthesis from Garg et al. CAV 2014 and POPL 2016, and Padhi et al. PLDI 2016
 *
 * This module mantains a set of functions-to-synthesize and a set of term
 * enumerators. When new terms are enumerated it tries to learn new candidate
 * solutions, which are verified outside this module. If verification fails a
 * refinement lemma is generated, which this module sends to the utility that
 * learns candidates.
 */
class CegisUnif : public Cegis
{
 public:
  CegisUnif(QuantifiersEngine* qe, CegConjecture* p);
  ~CegisUnif();
  /** initialize this class */
  bool initialize(Node n,
                  const std::vector<Node>& candidates,
                  std::vector<Node>& lemmas) override;
  /** Retrieves enumerators for constructing solutions
   *
   * Non-unification candidates have themselves as enumerators, while for
   * unification candidates we add their conditonal enumerators to enums if
   * their respective guards are set in the current model
   */
  void getTermList(const std::vector<Node>& candidates,
                   std::vector<Node>& enums) override;
  /** Tries to build new candidate solutions with new enumerated expressions
   *
   * This function relies on a data-driven unification-based approach for
   * constructing solutions for the functions-to-synthesize. See SygusUnifRl for
   * more details.
   *
   * Calls to this function are such that terms is the list of active
   * enumerators (returned by getTermList), and term_values are their current
   * model values. This function registers { terms -> terms_values } in
   * the database of values that have been enumerated, which are in turn used
   * for constructing candidate solutions when possible.
   *
   * This function also excludes models where (terms = terms_values) by adding
   * blocking clauses to lems. For example, for grammar:
   *   A -> A+A | x | 1 | 0
   * and a call where terms = { d } and term_values = { +( x, 1 ) }, it adds:
   *   ~G V ~is_+( d ) V ~is_x( d.1 ) V ~is_1( d.2 )
   * to lems, where G is active guard of the enumerator d (see
   * TermDatabaseSygus::getActiveGuardForEnumerator). This blocking clause
   * indicates that d should not be given the model value +( x, 1 ) anymore,
   * since { d -> +( x, 1 ) } has now been added to the database of this class.
   */
  bool constructCandidates(const std::vector<Node>& enums,
                           const std::vector<Node>& enum_values,
                           const std::vector<Node>& candidates,
                           std::vector<Node>& candidate_values,
                           std::vector<Node>& lems) override;

  /** Communicates refinement lemma to unification utility and external modules
   *
   * For the lemma to be sent to the external modules it adds a guard from the
   * parent conjecture which establishes that if the conjecture has a solution
   * then it must satisfy this refinement lemma
   *
   * For the lemma to be sent to the unification utility it purifies the
   * arguments of the function-to-synthensize such that all of its applications
   * are over concrete values. E.g.:
   *   f(f(f(0))) > 1
   * becomes
   *   f(0) != c1 v f(c1) != c2 v f(c2) > 1
   * in which c1 and c2 are concrete integer values
   *
   * Note that the lemma is in the deep embedding, which means that the above
   * example would actually correspond to
   *   eval(d, 0) != c1 v eval(d, c1) != c2 v eval(d, c2) > 1
   * in which d is the deep embedding of the function-to-synthesize f
  */
  void registerRefinementLemma(const std::vector<Node>& vars,
                               Node lem,
                               std::vector<Node>& lems) override;
  /** get next decision request */
  Node getNextDecisionRequest(unsigned& priority) override;

 private:
  /** sygus term database of d_qe */
  TermDbSygus* d_tds;
  /**
   * Sygus unif utility. This class implements the core algorithm (e.g. decision
   * tree learning) that this module relies upon.
   */
  SygusUnifRl d_sygus_unif;
  /** enumerator manager utility */
  CegisUnifEnumManager d_u_enum_manager;
  /**
   * list of conditonal enumerators to build solutions for candidates being
   * synthesized with unification techniques
   */
  std::vector<Node> d_cond_enums;
  /** map from enumerators to active guards */
  std::map<Node, Node> d_enum_to_active_guard;
  /* The null node */
  Node d_null;
}; /* class CegisUnif */

}  // namespace quantifiers
}  // namespace theory
}  // namespace CVC4

#endif
