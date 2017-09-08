/*********************                                                        */
/*! \file term_database_sygus.h
 ** \verbatim
 ** Top contributors (to current version):
 **   Andrew Reynolds
 ** This file is part of the CVC4 project.
 ** Copyright (c) 2009-2017 by the authors listed in the file AUTHORS
 ** in the top-level source directory) and their institutional affiliations.
 ** All rights reserved.  See the file COPYING in the top-level source
 ** directory for licensing information.\endverbatim
 **
 ** \brief term database sygus class
 **/

#include "cvc4_private.h"

#ifndef __CVC4__THEORY__QUANTIFIERS__TERM_DATABASE_SYGUS_H
#define __CVC4__THEORY__QUANTIFIERS__TERM_DATABASE_SYGUS_H

#include "theory/quantifiers/term_database.h"

namespace CVC4 {
namespace theory {
namespace quantifiers {

class SygusInvarianceTest {
protected:
  // check whether nvn[ x ] should be excluded
  virtual bool invariant( TermDbSygus * tds, Node nvn, Node x ) = 0;
public:
  bool is_invariant( TermDbSygus * tds, Node nvn, Node x ){
    if( invariant( tds, nvn, x ) ){
      d_update_nvn = nvn;
      return true;
    }else{
      return false;
    }
  }
  // result of the node after invariant replacements
  Node d_update_nvn;
};

class EvalSygusInvarianceTest : public SygusInvarianceTest {
public:
  Node d_conj;
  TNode d_var;
  std::map< Node, Node > d_visited;
  Node d_result;
protected:
  bool invariant( quantifiers::TermDbSygus * tds, Node nvn, Node x );
};

class TermDbSygus {
private:
  /** reference to the quantifiers engine */
  QuantifiersEngine* d_quantEngine;
  std::map< TypeNode, std::vector< Node > > d_fv[2];
  std::map< Node, TypeNode > d_fv_stype;
  std::map< Node, int > d_fv_num;
  bool hasFreeVar( Node n, std::map< Node, bool >& visited );
public:
  Node d_true;
  Node d_false;
public:
  TNode getFreeVar( TypeNode tn, int i, bool useSygusType = false );
  TNode getFreeVarInc( TypeNode tn, std::map< TypeNode, int >& var_count, bool useSygusType = false );
  bool isFreeVar( Node n ) { return d_fv_stype.find( n )!=d_fv_stype.end(); }
  int getVarNum( Node n ) { return d_fv_num[n]; }
  bool hasFreeVar( Node n );
private:
  std::map< TypeNode, std::map< int, Node > > d_generic_base;
  std::map< TypeNode, std::vector< Node > > d_generic_templ;
  bool getMatch( Node p, Node n, std::map< int, Node >& s );
  bool getMatch2( Node p, Node n, std::map< int, Node >& s, std::vector< int >& new_s );
public:
  bool getMatch( Node n, TypeNode st, int& index_found, std::vector< Node >& args, int index_exc = -1, int index_start = 0 );
private:
  void computeMinTypeDepthInternal( TypeNode root_tn, TypeNode tn, unsigned type_depth );
  bool involvesDivByZero( Node n, std::map< Node, bool >& visited );
private:
  // stores root
  std::map< Node, Node > d_measured_term;
  std::map< Node, Node > d_measured_term_active_guard;
  //information for sygus types
  std::map< TypeNode, TypeNode > d_register;  //stores sygus -> builtin type
  std::map< TypeNode, std::vector< Node > > d_var_list;
  std::map< TypeNode, std::map< int, Kind > > d_arg_kind;
  std::map< TypeNode, std::map< Kind, int > > d_kinds;
  std::map< TypeNode, std::map< int, Node > > d_arg_const;
  std::map< TypeNode, std::map< Node, int > > d_consts;
  std::map< TypeNode, std::map< Node, int > > d_ops;
  std::map< TypeNode, std::map< int, Node > > d_arg_ops;
  std::map< TypeNode, std::vector< int > > d_id_funcs;
  std::map< TypeNode, std::vector< Node > > d_const_list; //sorted list of constants for type
  std::map< TypeNode, unsigned > d_const_list_pos;
  std::map< TypeNode, std::map< Node, Node > > d_semantic_skolem;
  //information for builtin types
  std::map< TypeNode, std::map< int, Node > > d_type_value;
  std::map< TypeNode, Node > d_type_max_value;
  std::map< TypeNode, std::map< Node, std::map< int, Node > > > d_type_value_offset;
  std::map< TypeNode, std::map< Node, std::map< int, int > > > d_type_value_offset_status;
  //normalized map
  std::map< TypeNode, std::map< Node, Node > > d_normalized;
  std::map< TypeNode, std::map< Node, Node > > d_sygus_to_builtin;
  std::map< TypeNode, std::map< Node, Node > > d_builtin_const_to_sygus;
  // grammar information
  // root -> type -> _
  std::map< TypeNode, std::map< TypeNode, unsigned > > d_min_type_depth;
  //std::map< TypeNode, std::map< Node, std::map< std::map< int, bool > > > d_consider_const;
  // type -> cons -> _
  std::map< TypeNode, unsigned > d_min_term_size;
  std::map< TypeNode, std::map< unsigned, unsigned > > d_min_cons_term_size;
public:
  TermDbSygus( context::Context* c, QuantifiersEngine* qe );
  ~TermDbSygus(){}
  bool reset( Theory::Effort e );
  std::string identify() const { return "TermDbSygus"; }
public:
  /** register the sygus type */
  void registerSygusType( TypeNode tn );
  /** register a term that we will do enumerative search on */
  void registerMeasuredTerm( Node e, Node root, bool mkActiveGuard = false );
  /** is measured term */
  Node isMeasuredTerm( Node e );
  /** get active guard */
  Node getActiveGuardForMeasureTerm( Node e );
  /** get measured terms */
  void getMeasuredTerms( std::vector< Node >& mts );
public:  //general sygus utilities
  bool isRegistered( TypeNode tn );
  // get the minimum depth of type in its parent grammar
  unsigned getMinTypeDepth( TypeNode root_tn, TypeNode tn );
  // get the minimum size for a constructor term
  unsigned getMinTermSize( TypeNode tn );
  unsigned getMinConsTermSize( TypeNode tn, unsigned cindex );
public:
  TypeNode sygusToBuiltinType( TypeNode tn );
  int getKindConsNum( TypeNode tn, Kind k );
  int getConstConsNum( TypeNode tn, Node n );
  int getOpConsNum( TypeNode tn, Node n );
  bool hasKind( TypeNode tn, Kind k );
  bool hasConst( TypeNode tn, Node n );
  bool hasOp( TypeNode tn, Node n );
  Node getConsNumConst( TypeNode tn, int i );
  Node getConsNumOp( TypeNode tn, int i );
  Kind getConsNumKind( TypeNode tn, int i );
  bool isKindArg( TypeNode tn, int i );
  bool isConstArg( TypeNode tn, int i );
  unsigned getNumIdFuncs( TypeNode tn );
  unsigned getIdFuncIndex( TypeNode tn, unsigned i );
  /** get arg type */
  TypeNode getArgType( const DatatypeConstructor& c, int i );
  /** get first occurrence */
  int getFirstArgOccurrence( const DatatypeConstructor& c, TypeNode tn );
  /** is type match */
  bool isTypeMatch( const DatatypeConstructor& c1, const DatatypeConstructor& c2 );
  /** isAntisymmetric */
  bool isAntisymmetric( Kind k, Kind& dk );
  /** is idempotent arg */
  bool isIdempotentArg( Node n, Kind ik, int arg );
  /** is singular arg */
  Node isSingularArg( Node n, Kind ik, int arg );
  /** get offset arg */
  bool hasOffsetArg( Kind ik, int arg, int& offset, Kind& ok );
  /** get value */
  Node getTypeValue( TypeNode tn, int val );
  /** get value */
  Node getTypeValueOffset( TypeNode tn, Node val, int offset, int& status );
  /** get value */
  Node getTypeMaxValue( TypeNode tn );
  TypeNode getSygusTypeForVar( Node v );
  Node getGenericBase( TypeNode tn, const Datatype& dt, int c );
  Node mkGeneric( const Datatype& dt, int c, std::map< TypeNode, int >& var_count, std::map< int, Node >& pre );
  Node sygusToBuiltin( Node n, TypeNode tn );
  Node sygusToBuiltin( Node n ) { return sygusToBuiltin( n, n.getType() ); }
  Node sygusSubstituted( TypeNode tn, Node n, std::vector< Node >& args );
  Node builtinToSygusConst( Node c, TypeNode tn, int rcons_depth = 0 );
  Node getSygusNormalized( Node n, std::map< TypeNode, int >& var_count, std::map< Node, Node >& subs );
  Node getNormalized( TypeNode t, Node prog, bool do_pre_norm = false, bool do_post_norm = true );
  unsigned getSygusTermSize( Node n );
  // returns size
  unsigned getSygusConstructors( Node n, std::vector< Node >& cons );
  /** given a term, construct an equivalent smaller one that respects syntax */
  Node minimizeBuiltinTerm( Node n );
  /** given a term, expand it into more basic components */
  Node expandBuiltinTerm( Node n );
  /** get comparison kind */
  Kind getComparisonKind( TypeNode tn );
  Kind getPlusKind( TypeNode tn, bool is_neg = false );
  bool doCompare( Node a, Node b, Kind k );
  // get semantic skolem for n (a sygus term whose builtin version is n)
  Node getSemanticSkolem( TypeNode tn, Node n, bool doMk = true );
  /** involves div-by-zero */
  bool involvesDivByZero( Node n );
  
  /** get operator kind */
  static Kind getOperatorKind( Node op );
  /** print sygus term */
  static void printSygusTerm( std::ostream& out, Node n, std::vector< Node >& lvs );

  /** get anchor */
  static Node getAnchor( Node n );
  static unsigned getAnchorDepth( Node n );
  
public: // for symmetry breaking
  bool considerArgKind( TypeNode tn, TypeNode tnp, Kind k, Kind pk, int arg );
  bool considerConst( TypeNode tn, TypeNode tnp, Node c, Kind pk, int arg );
  bool considerConst( const Datatype& pdt, TypeNode tnp, Node c, Kind pk, int arg );
  int solveForArgument( TypeNode tnp, unsigned cindex, unsigned arg );
  
//for eager instantiation
private:
  std::map< Node, std::map< Node, bool > > d_subterms;
  std::map< Node, std::vector< Node > > d_evals;
  std::map< Node, std::vector< std::vector< Node > > > d_eval_args;
  std::map< Node, std::vector< bool > > d_eval_args_const;
  std::map< Node, std::map< Node, unsigned > > d_node_mv_args_proc;

  void getExplanationFor( TermRecBuild& trb, Node n, Node vn, std::vector< Node >& exp, std::map< TypeNode, int >& var_count,
                          SygusInvarianceTest& et, Node vnr, Node& vnr_exp, int& sz );
public:
  void registerEvalTerm( Node n );
  void registerModelValue( Node n, Node v, std::vector< Node >& exps, std::vector< Node >& terms, std::vector< Node >& vals );
  Node unfold( Node en, std::map< Node, Node >& vtm, std::vector< Node >& exp, bool track_exp = true );
  Node unfold( Node en ){
    std::map< Node, Node > vtm;
    std::vector< Node > exp;
    return unfold( en, vtm, exp, false );
  }
  Node getEagerUnfold( Node n, std::map< Node, Node >& visited );
  // returns straightforward exp => n = vn
  void getExplanationForConstantEquality( Node n, Node vn, std::vector< Node >& exp );
  void getExplanationForConstantEquality( Node n, Node vn, std::vector< Node >& exp, std::map< unsigned, bool >& cexc );
  Node getExplanationForConstantEquality( Node n, Node vn );
  Node getExplanationForConstantEquality( Node n, Node vn, std::map< unsigned, bool >& cexc );
  // we have n = vn => eval( n ) = bvr, returns exp => eval( n ) = bvr
  //   ensures the explanation still allows for vnr
  void getExplanationFor( Node n, Node vn, std::vector< Node >& exp, SygusInvarianceTest& et, Node vnr, unsigned& sz );
  void getExplanationFor( Node n, Node vn, std::vector< Node >& exp, SygusInvarianceTest& et );
  // builtin evaluation, returns rewrite( bn [ args / vars(tn) ] )
  Node evaluateBuiltin( TypeNode tn, Node bn, std::vector< Node >& args );
  Node evaluateBuiltin( TypeNode tn, Node bn, Node ar, unsigned i );
  // evaluate with unfolding
  Node evaluateWithUnfolding( Node n, std::map< Node, Node >& visited );
  Node evaluateWithUnfolding( Node n );
//for calculating redundant operators
private:
  //whether each constructor is redundant
  // 0 : not redundant, 1 : redundant, 2 : partially redundant
  std::map< TypeNode, std::vector< int > > d_sygus_red_status;
  // type to (rewritten) to original
  std::map< TypeNode, std::map< Node, Node > > d_gen_terms;
  std::map< TypeNode, std::map< Node, bool > > d_gen_redundant;
  //compute generic redundant
  bool computeGenericRedundant( TypeNode tn, Node g );
public:
  bool isGenericRedundant( TypeNode tn, unsigned i );
  
//sygus pbe
private:
  std::map< Node, std::vector< std::vector< Node > > > d_pbe_exs;
  std::map< Node, std::vector< Node > > d_pbe_exos;
  std::map< Node, unsigned > d_pbe_term_id;
private:
  class PbeTrie {
  private:
    Node addPbeExampleEval( TypeNode etn, Node e, Node b, std::vector< Node >& ex, quantifiers::TermDbSygus * tds, unsigned index, unsigned ntotal );
  public:
    PbeTrie(){}
    ~PbeTrie(){}
    Node d_lazy_child;
    std::map< Node, PbeTrie > d_children;
    void clear() { d_children.clear(); }
    Node addPbeExample( TypeNode etn, Node e, Node b, TermDbSygus * tds, unsigned index, unsigned ntotal );
  };
  std::map< Node, std::map< TypeNode, PbeTrie > > d_pbe_trie;
public:
  /** register examples for an enumerative search term. 
      This should be a comprehensive set of examples. */
  void registerPbeExamples( Node e, std::vector< std::vector< Node > >& exs, 
                            std::vector< Node >& exos, std::vector< Node >& exts );
  /** get examples */
  bool hasPbeExamples( Node e );
  unsigned getNumPbeExamples( Node e );
  /** return value is the required value for the example */
  void getPbeExample( Node e, unsigned i, std::vector< Node >& ex );
  Node getPbeExampleOut( Node e, unsigned i );
  int getPbeExampleId( Node n );
  /** add the search val, returns an equivalent value (possibly the same) */
  Node addPbeSearchVal( TypeNode tn, Node e, Node bvr );

// extended rewriting
private:
  std::map< Node, Node > d_ext_rewrite_cache;
  Node extendedRewritePullIte( Node n );
public:
  Node extendedRewrite( Node n );
  
// for grammar construction
private:
  TypeNode mkUnresolvedType(const std::string& name, std::set<Type>& unres);
  void mkSygusConstantsForType( TypeNode type, std::vector<CVC4::Node>& ops );
  void collectSygusGrammarTypesFor( TypeNode range, std::vector< TypeNode >& types, std::map< TypeNode, std::vector< DatatypeConstructorArg > >& sels );
  void mkSygusDefaultGrammar( TypeNode range, Node bvl, const std::string& fun, std::map< TypeNode, std::vector< Node > >& extra_cons, std::vector< CVC4::Datatype >& datatypes, std::set<Type>& unres );
  TypeNode mkSygusTemplateTypeRec( Node templ, Node templ_arg, TypeNode templ_arg_sygus_type, Node bvl, 
                                const std::string& fun, unsigned& tcount );
public:
  TypeNode mkSygusDefaultType( TypeNode range, Node bvl, const std::string& fun, std::map< TypeNode, std::vector< Node > >& extra_cons );
  TypeNode mkSygusDefaultType( TypeNode range, Node bvl, const std::string& fun ){
    std::map< TypeNode, std::vector< Node > > extra_cons;
    return mkSygusDefaultType( range, bvl, fun, extra_cons );
  }
  TypeNode mkSygusTemplateType( Node templ, Node templ_arg, TypeNode templ_arg_sygus_type, Node bvl, const std::string& fun );
};

}/* CVC4::theory::quantifiers namespace */
}/* CVC4::theory namespace */
}/* CVC4 namespace */

#endif /* __CVC4__THEORY__QUANTIFIERS__TERM_DATABASE_H */
