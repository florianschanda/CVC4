/*********************                                                        */
/*! \file ce_guided_single_inv.cpp
 ** \verbatim
 ** Top contributors (to current version):
 **   Andrew Reynolds, Tim King
 ** This file is part of the CVC4 project.
 ** Copyright (c) 2009-2017 by the authors listed in the file AUTHORS
 ** in the top-level source directory) and their institutional affiliations.
 ** All rights reserved.  See the file COPYING in the top-level source
 ** directory for licensing information.\endverbatim
 **
 ** \brief utility for processing single invocation synthesis conjectures
 **
 **/
#include "theory/quantifiers/ce_guided_single_inv.h"

#include "expr/datatype.h"
#include "options/quantifiers_options.h"
#include "theory/quantifiers/ce_guided_instantiation.h"
#include "theory/quantifiers/ce_guided_single_inv_ei.h"
#include "theory/quantifiers/first_order_model.h"
#include "theory/quantifiers/quant_util.h"
#include "theory/quantifiers/term_database_sygus.h"
#include "theory/quantifiers/trigger.h"
#include "theory/theory_engine.h"

using namespace CVC4;
using namespace CVC4::kind;
using namespace CVC4::theory;
using namespace CVC4::theory::quantifiers;
using namespace std;

namespace CVC4 {

bool CegqiOutputSingleInv::doAddInstantiation( std::vector< Node >& subs ) {
  return d_out->doAddInstantiation( subs );
}

bool CegqiOutputSingleInv::isEligibleForInstantiation( Node n ) {
  return d_out->isEligibleForInstantiation( n );
}

bool CegqiOutputSingleInv::addLemma( Node n ) {
  return d_out->addLemma( n );
}

CegConjectureSingleInv::CegConjectureSingleInv(QuantifiersEngine* qe,
                                               CegConjecture* p)
    : d_qe(qe),
      d_parent(p),
      d_sip(new SingleInvocationPartition),
      d_sol(new CegConjectureSingleInvSol(qe)),
      d_ei(NULL),
      d_cosi(new CegqiOutputSingleInv(this)),
      d_cinst(NULL),
      d_c_inst_match_trie(NULL),
      d_has_ites(true) {
  //  third and fourth arguments set to (false,false) until we have solution
  //  reconstruction for delta and infinity
  d_cinst = new CegInstantiator(d_qe, d_cosi, false, false);

  if (options::incrementalSolving()) {
    d_c_inst_match_trie = new inst::CDInstMatchTrie(qe->getUserContext());
  }

  if (options::cegqiSingleInvPartial()) {
    d_ei = new CegEntailmentInfer(qe, d_sip);
  }
}

CegConjectureSingleInv::~CegConjectureSingleInv() {
  if (d_c_inst_match_trie) {
    delete d_c_inst_match_trie;
  }
  delete d_cinst;
  delete d_cosi;
  if (d_ei) {
    delete d_ei;
  }
  delete d_sol;  // (new CegConjectureSingleInvSol(qe)),
  delete d_sip;  // d_sip(new SingleInvocationPartition),
}



Node CegConjectureSingleInv::convertToEmbedding( Node n, std::map< Node, Node >& synth_fun_vars, std::map< Node, Node >& visited ){
  std::map< Node, Node >::iterator it = visited.find( n );
  if( it==visited.end() ){
    Node ret = n;
    
    std::vector< Node > children;
    bool childChanged = false;
    bool madeOp = false;
    Kind ret_k = n.getKind();
    Node op;
    if( n.getNumChildren()>0 ){
      if( n.getKind()==kind::APPLY_UF ){
        op = n.getOperator();
      }
    }else{
      op = n;
    }
    // is it a synth function?
    std::map< Node, Node >::iterator its = synth_fun_vars.find( op );
    if( its!=synth_fun_vars.end() ){
      Assert( its->second.getType().isDatatype() );
      // make into evaluation function
      const Datatype& dt = ((DatatypeType)its->second.getType().toType()).getDatatype();
      Assert( dt.isSygus() );
      children.push_back( Node::fromExpr( dt.getSygusEvaluationFunc() ) );
      children.push_back( its->second );
      madeOp = true;
      childChanged = true;
      ret_k = kind::APPLY_UF;
    }
    if( n.getNumChildren()>0 || childChanged ){
      if( !madeOp ){
        if( n.getMetaKind() == kind::metakind::PARAMETERIZED ){
          children.push_back( n.getOperator() );
        }
      }
      for( unsigned i=0; i<n.getNumChildren(); i++ ){
        Node nc = convertToEmbedding( n[i], synth_fun_vars, visited ); 
        childChanged = childChanged || nc!=n[i];
        children.push_back( nc );
      }
      if( childChanged ){
        ret = NodeManager::currentNM()->mkNode( ret_k, children );
      }
    }
    visited[n] = ret;
    return ret;
  }else{
    return it->second;
  }
}

void CegConjectureSingleInv::collectConstants( Node n, std::map< TypeNode, std::vector< Node > >& consts, std::map< Node, bool >& visited ) {
  if( visited.find( n )==visited.end() ){
    visited[n] = true;
    if( n.isConst() ){
      TypeNode tn = n.getType();
      Node nc = n;
      if( tn.isReal() ){
        nc = NodeManager::currentNM()->mkConst( n.getConst<Rational>().abs() );
      }
      if( std::find( consts[tn].begin(), consts[tn].end(), nc )==consts[tn].end() ){
        Trace("cegqi-debug") << "...consider const : " << nc << std::endl;
        consts[tn].push_back( nc );
      }
    }
    
    for( unsigned i=0; i<n.getNumChildren(); i++ ){
      collectConstants( n[i], consts, visited );
    }
  }
}

void CegConjectureSingleInv::getInitialSingleInvLemma( std::vector< Node >& lems ) {
  Assert( d_si_guard.isNull() );
  //single invocation guard
  d_si_guard = Rewriter::rewrite( NodeManager::currentNM()->mkSkolem( "G", NodeManager::currentNM()->booleanType() ) );
  d_si_guard = d_qe->getValuation().ensureLiteral( d_si_guard );
  AlwaysAssert( !d_si_guard.isNull() );
  d_qe->getOutputChannel().requirePhase( d_si_guard, true );

  if( !d_single_inv.isNull() ) {
    //make for new var/sk
    d_single_inv_var.clear();
    d_single_inv_sk.clear();
    Node inst;
    if( d_single_inv.getKind()==FORALL ){
      for( unsigned i=0; i<d_single_inv[0].getNumChildren(); i++ ){
        std::stringstream ss;
        ss << "k_" << d_single_inv[0][i];
        Node k = NodeManager::currentNM()->mkSkolem( ss.str(), d_single_inv[0][i].getType(), "single invocation function skolem" );
        d_single_inv_var.push_back( d_single_inv[0][i] );
        d_single_inv_sk.push_back( k );
        d_single_inv_sk_index[k] = i;
      }
      inst = d_single_inv[1].substitute( d_single_inv_var.begin(), d_single_inv_var.end(), d_single_inv_sk.begin(), d_single_inv_sk.end() );
    }else{
      inst = d_single_inv;
    }
    inst = TermDb::simpleNegate( inst );
    Trace("cegqi-si") << "Single invocation initial lemma : " << inst << std::endl;

    //register with the instantiator
    Node ginst = NodeManager::currentNM()->mkNode( OR, d_si_guard.negate(), inst );
    lems.push_back( ginst );
    //make and register the instantiator
    if( d_cinst ){
      delete d_cinst;
    }
    d_cinst = new CegInstantiator( d_qe, d_cosi, false, false );
    d_cinst->registerCounterexampleLemma( lems, d_single_inv_sk );
  }
}

void CegConjectureSingleInv::initialize( Node si_q ) {
  Assert( d_orig_quant.isNull() );
  d_orig_quant = si_q;
  // infer single invocation-ness
  std::vector< Node > progs;
  for( unsigned i=0; i<si_q[0].getNumChildren(); i++ ){
    Node v = si_q[0][i];
    Node sf = v.getAttribute(SygusSynthFunAttribute());
    progs.push_back( sf );
  }
  // compute single invocation partition
  bool singleInvocation;
  if( options::cegqiSingleInvMode()==CEGQI_SI_MODE_NONE ){  
    singleInvocation = false;
  }else{
    Node qq;
    if( si_q[1].getKind()==NOT && si_q[1][0].getKind()==FORALL ){
      qq = si_q[1][0][1];
    }else{
      qq = TermDb::simpleNegate( si_q[1] );
    }
    //process the single invocation-ness of the property
    d_sip->init( progs, qq );
    Trace("cegqi-si") << "- Partitioned to single invocation parts : " << std::endl;
    d_sip->debugPrint( "cegqi-si" );

    //map from program to bound variables
    std::vector< Node > order_vars;
    std::map< Node, Node > single_inv_app_map;
    for( unsigned j=0; j<progs.size(); j++ ){
      Node prog = progs[j];
      std::map< Node, Node >::iterator it_fov = d_sip->d_func_fo_var.find( prog );
      if( it_fov!=d_sip->d_func_fo_var.end() ){
        Node pv = it_fov->second;
        Assert( d_sip->d_func_inv.find( prog )!=d_sip->d_func_inv.end() );
        Node inv = d_sip->d_func_inv[prog];
        single_inv_app_map[prog] = inv;
        Trace("cegqi-si") << "  " << pv << ", " << inv << " is associated with program " << prog << std::endl;
        d_prog_to_sol_index[prog] = order_vars.size();
        order_vars.push_back( pv );
      }else{
        Trace("cegqi-si") << "  " << prog << " has no fo var." << std::endl;
      }
    }
    //reorder the variables
    Assert( d_sip->d_func_vars.size()==order_vars.size() );
    d_sip->d_func_vars.clear();
    d_sip->d_func_vars.insert( d_sip->d_func_vars.begin(), order_vars.begin(), order_vars.end() );


    //check if it is single invocation
    if( !d_sip->d_conjuncts[1].empty() ){
      singleInvocation = false;
      if( options::sygusInvTemplMode() != SYGUS_INV_TEMPL_MODE_NONE ){
        //if we are doing invariant templates, then construct the template
        Trace("cegqi-si") << "- Do transition inference..." << std::endl;
        d_ti[si_q].process( qq );
        Trace("cegqi-inv") << std::endl;
        std::map< Node, Node > prog_templ;
        if( !d_ti[si_q].d_func.isNull() ){
          // map the program back via non-single invocation map
          Node prog = d_ti[si_q].d_func;
          Assert( d_prog_templ_vars[prog].empty() );
          d_prog_templ_vars[prog].insert( d_prog_templ_vars[prog].end(), d_ti[si_q].d_vars.begin(), d_ti[si_q].d_vars.end() );
          d_trans_pre[prog] = d_ti[si_q].getComponent( 1 );
          d_trans_post[prog] = d_ti[si_q].getComponent( -1 );
          Trace("cegqi-inv") << "   precondition : " << d_trans_pre[prog] << std::endl;
          Trace("cegqi-inv") << "  postcondition : " << d_trans_post[prog] << std::endl;
          Node invariant = single_inv_app_map[prog];
          invariant = invariant.substitute( d_sip->d_si_vars.begin(), d_sip->d_si_vars.end(), d_prog_templ_vars[prog].begin(), d_prog_templ_vars[prog].end() );
          Trace("cegqi-inv") << "      invariant : " << invariant << std::endl;
            
          //construct template
          d_templ_arg[prog] = NodeManager::currentNM()->mkSkolem( "I", invariant.getType() );
          if( options::sygusInvAutoUnfold() ){
            if( d_ti[si_q].isComplete() ){
              Trace("cegqi-inv-auto-unfold") << "Automatic deterministic unfolding... " << std::endl;
              // auto-unfold
              DetTrace dt;
              int init_dt = d_ti[si_q].initializeTrace( dt );
              if( init_dt==0 ){
                Trace("cegqi-inv-auto-unfold") << "  Init : ";
                dt.print("cegqi-inv-auto-unfold");
                Trace("cegqi-inv-auto-unfold") << std::endl;
                unsigned counter = 0;
                unsigned status = 0;
                while( counter<100 && status==0 ){
                  status = d_ti[si_q].incrementTrace( dt );
                  counter++;
                  Trace("cegqi-inv-auto-unfold") << "  #" << counter << " : ";
                  dt.print("cegqi-inv-auto-unfold");
                  Trace("cegqi-inv-auto-unfold") << "...status = " << status << std::endl;
                }
                if( status==1 ){
                  // we have a trivial invariant
                  d_templ[prog] = d_ti[si_q].constructFormulaTrace( dt );
                  Trace("cegqi-inv") << "By finite deterministic terminating trace, a solution invariant is : " << std::endl;
                  Trace("cegqi-inv") << "   " << d_templ[prog] << std::endl;
                  // FIXME : this should be uncessary
                  d_templ[prog] = NodeManager::currentNM()->mkNode( AND, d_templ[prog], d_templ_arg[prog] );
                }
              }else{
                Trace("cegqi-inv-auto-unfold") << "...failed initialize." << std::endl;
              }
            }
          }
          if( d_templ[prog].isNull() ){
            if( options::sygusInvTemplMode() == SYGUS_INV_TEMPL_MODE_PRE ){
              //d_templ[prog] = NodeManager::currentNM()->mkNode( AND, NodeManager::currentNM()->mkNode( OR, d_trans_pre[prog], invariant ), d_trans_post[prog] );
              d_templ[prog] = NodeManager::currentNM()->mkNode( OR, d_trans_pre[prog], d_templ_arg[prog] );
            }else{
              Assert( options::sygusInvTemplMode() == SYGUS_INV_TEMPL_MODE_POST );
              //d_templ[prog] = NodeManager::currentNM()->mkNode( OR, d_trans_pre[prog], NodeManager::currentNM()->mkNode( AND, d_trans_post[prog], invariant ) );
              d_templ[prog] = NodeManager::currentNM()->mkNode( AND, d_trans_post[prog], d_templ_arg[prog] );
            }
          }
          TNode iv = d_templ_arg[prog];
          TNode is = invariant;
          Node templ = d_templ[prog].substitute( iv, is );
          //std::map< Node, Node > visitedn;
          //templ = addDeepEmbedding( templ, visitedn );
          Trace("cegqi-inv") << "       template : " << templ << std::endl;
          prog_templ[prog] = templ;
        }
      }
    }else{
      //we are fully single invocation
      singleInvocation = true;
    }

    if( singleInvocation ){
      d_single_inv = d_sip->getSingleInvocation();
      d_single_inv = TermDb::simpleNegate( d_single_inv );
      if( !d_sip->d_func_vars.empty() ){
        Node pbvl = NodeManager::currentNM()->mkNode( BOUND_VAR_LIST, d_sip->d_func_vars );
        d_single_inv = NodeManager::currentNM()->mkNode( FORALL, pbvl, d_single_inv );
      }
      //now, introduce the skolems
      for( unsigned i=0; i<d_sip->d_si_vars.size(); i++ ){
        Node v = NodeManager::currentNM()->mkSkolem( "a", d_sip->d_si_vars[i].getType(), "single invocation arg" );
        d_single_inv_arg_sk.push_back( v );
      }
      d_single_inv = d_single_inv.substitute( d_sip->d_si_vars.begin(), d_sip->d_si_vars.end(), d_single_inv_arg_sk.begin(), d_single_inv_arg_sk.end() );
      Trace("cegqi-si") << "Single invocation formula is : " << d_single_inv << std::endl;
      if( options::cbqiPreRegInst() && d_single_inv.getKind()==FORALL ){
        //just invoke the presolve now
        d_cinst->presolve( d_single_inv );
      }
      if( !isFullySingleInvocation() ){
        //initialize information as next single invocation conjecture
        initializeNextSiConjecture();
        Trace("cegqi-si") << "Non-single invocation formula is : " << d_nsingle_inv << std::endl;
        Trace("cegqi-si") << "Full specification is : " << d_full_inv << std::endl;
        //add full specification lemma : will use for testing infeasibility/deriving entailments
        d_full_guard = Rewriter::rewrite( NodeManager::currentNM()->mkSkolem( "GF", NodeManager::currentNM()->booleanType() ) );
        d_full_guard = d_qe->getValuation().ensureLiteral( d_full_guard );
        AlwaysAssert( !d_full_guard.isNull() );
        d_qe->getOutputChannel().requirePhase( d_full_guard, true );
        Node fbvl;
        if( !d_sip->d_all_vars.empty() ){
          fbvl = NodeManager::currentNM()->mkNode( BOUND_VAR_LIST, d_sip->d_all_vars );
        }
        //should construct this conjunction directly since miniscoping is disabled
        std::vector< Node > flem_c;
        for( unsigned i=0; i<d_sip->d_conjuncts[2].size(); i++ ){
          Node flemi = d_sip->d_conjuncts[2][i];
          if( !fbvl.isNull() ){
            flemi = NodeManager::currentNM()->mkNode( FORALL, fbvl, flemi );
          }
          flem_c.push_back( flemi );
        }
        Node flem = flem_c.empty() ? d_qe->getTermDatabase()->d_true : ( flem_c.size()==1 ? flem_c[0] : NodeManager::currentNM()->mkNode( AND, flem_c ) );
        flem = NodeManager::currentNM()->mkNode( OR, d_full_guard.negate(), flem );
        flem = Rewriter::rewrite( flem );
        Trace("cegqi-lemma") << "Cegqi::Lemma : full specification " << flem << std::endl;
        d_qe->getOutputChannel().lemma( flem );
      }
    }else{
      Trace("cegqi-si") << "Formula is not single invocation." << std::endl;
      if( options::cegqiSingleInvAbort() ){
        Notice() << "Property is not single invocation." << std::endl;
        exit( 1 );
      }
    }
  }
  
  // now, construct the grammar
  Trace("cegqi") << "CegConjecture : convert to deep embedding..." << std::endl;
  std::map< TypeNode, std::vector< Node > > extra_cons;
  Trace("cegqi") << "CegConjecture : collect constants..." << std::endl;
  if( options::sygusAddConstGrammar() ){
    std::map< Node, bool > visited;
    collectConstants( si_q[1], extra_cons, visited );
  }
  

  //convert to deep embedding
  std::vector< Node > qchildren;
  std::map< Node, Node > visited;
  std::map< Node, Node > synth_fun_vars;
  std::vector< Node > ebvl;
  for( unsigned i=0; i<si_q[0].getNumChildren(); i++ ){
    Node v = si_q[0][i];
    Node sf = v.getAttribute(SygusSynthFunAttribute());
    Assert( !sf.isNull() );
    Node sfvl = sf.getAttribute(SygusSynthFunVarListAttribute());
    // sfvl may be null for constant synthesis functions
    Trace("cegqi-debug") << "...sygus var list associated with " << sf << " is " << sfvl << std::endl;
    TypeNode tn;
    std::stringstream ss;
    ss << sf;
    if( v.getType().isDatatype() && ((DatatypeType)v.getType().toType()).getDatatype().isSygus() ){
      tn = v.getType();
    }else{
      // make the default grammar
      tn = d_qe->getTermDatabaseSygus()->mkSygusDefaultType( v.getType(), sfvl, ss.str(), extra_cons );
    }
    // if there is a template for this argument, make a sygus type on top of it
    std::map< Node, Node >::iterator itt = d_templ.find( sf );
    if( itt!=d_templ.end() ){
      Node templ = itt->second;
      std::vector< Node > vars;
      vars.insert( vars.end(), d_prog_templ_vars[sf].begin(), d_prog_templ_vars[sf].end() );
      std::vector< Node > subs;
      for( unsigned j=0; j<sfvl.getNumChildren(); j++ ){
        subs.push_back( sfvl[j] );
      }
      Assert( vars.size()==subs.size() );
      templ = templ.substitute( vars.begin(), vars.end(), subs.begin(), subs.end() );
      if( Trace.isOn("cegqi-debug") ){
        Trace("cegqi-debug") << "Template for " << sf << " is : " << templ << " with arg " << d_templ_arg[sf] << std::endl;
        Trace("cegqi-debug") << "  embed this template as a grammar..." << std::endl;
      }
      tn = d_qe->getTermDatabaseSygus()->mkSygusTemplateType( templ, d_templ_arg[sf], tn, sfvl, ss.str() );
    }
    d_qe->getTermDatabaseSygus()->registerSygusType( tn );

    // ev is the first-order variable corresponding to this synth fun
    std::stringstream ssf;
    ssf << "f" << sf;
    Node ev = NodeManager::currentNM()->mkBoundVar( ssf.str(), tn ); 
    ebvl.push_back( ev );
    synth_fun_vars[sf] = ev;
    Trace("cegqi") << "...embedding synth fun : " << sf << " -> " << ev << std::endl;
  }
  qchildren.push_back( NodeManager::currentNM()->mkNode( kind::BOUND_VAR_LIST, ebvl ) );
  qchildren.push_back( convertToEmbedding( si_q[1], synth_fun_vars, visited ) );
  if( si_q.getNumChildren()==3 ){
    qchildren.push_back( si_q[2] );
  }
  Node q = NodeManager::currentNM()->mkNode( kind::FORALL, qchildren );
  Trace("cegqi") << "CegConjecture : converted to embedding : " << q << std::endl;
  Trace("ajr-temp") << "Converted to embedding : " << q << std::endl;
  d_quant = q;


  bool is_syntax_restricted = false;
  for( unsigned i=0; i<q[0].getNumChildren(); i++ ){
    //check whether all types have ITE
    TypeNode tn = q[0][i].getType();
    d_qe->getTermDatabaseSygus()->registerSygusType( tn );
    if( !d_qe->getTermDatabaseSygus()->sygusToBuiltinType( tn ).isBoolean() ){
      if( !d_qe->getTermDatabaseSygus()->hasKind( tn, ITE ) ){
        d_has_ites = false;
      }
    }
    Assert( tn.isDatatype() );
    const Datatype& dt = ((DatatypeType)(tn).toType()).getDatatype();
    Assert( dt.isSygus() );
    if( !dt.getSygusAllowAll() ){
      is_syntax_restricted = true;
    }
  }
  if( options::cegqiSingleInvMode()==CEGQI_SI_MODE_USE && is_syntax_restricted ){
    singleInvocation = false;
    Trace("cegqi-si") << "...grammar is restricted, do not use single invocation techniques." << std::endl;
  }
}

void CegConjectureSingleInv::initializeNextSiConjecture() {
  Trace("cegqi-nsi") << "NSI : initialize next candidate conjecture..." << std::endl;
  if( d_single_inv.isNull() ){
    if( d_ei->getEntailedConjecture( d_single_inv, d_single_inv_exp ) ){
      Trace("cegqi-nsi") << "NSI : got : " << d_single_inv << std::endl;
      Trace("cegqi-nsi") << "NSI : exp : " << d_single_inv_exp << std::endl;
    }else{
      Trace("cegqi-nsi") << "NSI : failed to construct next conjecture." << std::endl;
      Notice() << "Incomplete due to --cegqi-si-partial." << std::endl;
      exit( 10 );
    }
  }else{
    //initial call
    Trace("cegqi-nsi") << "NSI : have : " << d_single_inv << std::endl;
    Assert( d_single_inv_exp.isNull() );
  }

  d_si_guard = Node::null();
  d_ns_guard = Rewriter::rewrite( NodeManager::currentNM()->mkSkolem( "GS", NodeManager::currentNM()->booleanType() ) );
  d_ns_guard = d_qe->getValuation().ensureLiteral( d_ns_guard );
  AlwaysAssert( !d_ns_guard.isNull() );
  d_qe->getOutputChannel().requirePhase( d_ns_guard, true );
  d_lemmas_produced.clear();
  if( options::incrementalSolving() ){
    delete d_c_inst_match_trie;
    d_c_inst_match_trie = new inst::CDInstMatchTrie( d_qe->getUserContext() );
  }else{
    d_inst_match_trie.clear();
  }
  Trace("cegqi-nsi") << "NSI : initialize next candidate conjecture, ns guard = " << d_ns_guard << std::endl;
  Trace("cegqi-nsi") << "NSI : conjecture is " << d_single_inv << std::endl;
}

bool CegConjectureSingleInv::doAddInstantiation( std::vector< Node >& subs ){
  std::stringstream siss;
  if( Trace.isOn("cegqi-si-inst-debug") || Trace.isOn("cegqi-engine") ){
    siss << "  * single invocation: " << std::endl;
    for( unsigned j=0; j<d_single_inv_sk.size(); j++ ){
      Assert( d_sip->d_fo_var_to_func.find( d_single_inv[0][j] )!=d_sip->d_fo_var_to_func.end() );
      Node op = d_sip->d_fo_var_to_func[d_single_inv[0][j]];
      //Assert( d_nsi_op_map_to_prog.find( op )!=d_nsi_op_map_to_prog.end() );
      //Node prog = d_nsi_op_map_to_prog[op];
      Node prog = op;
      siss << "    * " << prog;
      siss << " (" << d_single_inv_sk[j] << ")";
      siss << " -> " << subs[j] << std::endl;
    }
  }
  bool alreadyExists;
  if( options::incrementalSolving() ){
    alreadyExists = !d_c_inst_match_trie->addInstMatch( d_qe, d_single_inv, subs, d_qe->getUserContext() );
  }else{
    alreadyExists = !d_inst_match_trie.addInstMatch( d_qe, d_single_inv, subs );
  }
  Trace("cegqi-si-inst-debug") << siss.str();
  Trace("cegqi-si-inst-debug") << "  * success = " << !alreadyExists << std::endl;
  if( alreadyExists ){
    return false;
  }else{
    Trace("cegqi-engine") << siss.str() << std::endl;
    Assert( d_single_inv_var.size()==subs.size() );
    Node lem = d_single_inv[1].substitute( d_single_inv_var.begin(), d_single_inv_var.end(), subs.begin(), subs.end() );
    if( d_qe->getTermDatabase()->containsVtsTerm( lem ) ){
      Trace("cegqi-engine-debug") << "Rewrite based on vts symbols..." << std::endl;
      lem = d_qe->getTermDatabase()->rewriteVtsSymbols( lem );
    }
    Trace("cegqi-engine-debug") << "Rewrite..." << std::endl;
    lem = Rewriter::rewrite( lem );
    Trace("cegqi-si") << "Single invocation lemma : " << lem << std::endl;
    if( std::find( d_lemmas_produced.begin(), d_lemmas_produced.end(), lem )==d_lemmas_produced.end() ){
      d_curr_lemmas.push_back( lem );
      d_lemmas_produced.push_back( lem );
      d_inst.push_back( std::vector< Node >() );
      d_inst.back().insert( d_inst.back().end(), subs.begin(), subs.end() );
    }
    return true;
  }
}

bool CegConjectureSingleInv::isEligibleForInstantiation( Node n ) {
  return n.getKind()!=SKOLEM || std::find( d_single_inv_arg_sk.begin(), d_single_inv_arg_sk.end(), n )!=d_single_inv_arg_sk.end();
}

bool CegConjectureSingleInv::addLemma( Node n ) {
  d_curr_lemmas.push_back( n );
  return true;
}

bool CegConjectureSingleInv::check( std::vector< Node >& lems ) {
  if( !d_single_inv.isNull() ) {
    Trace("cegqi-si-debug") << "CegConjectureSingleInv::check..." << std::endl;
    if( !d_ns_guard.isNull() ){
      //if partially single invocation, check if we have constructed a candidate by refutation
      bool value;
      if( d_qe->getValuation().hasSatValue( d_ns_guard, value ) ) {
        if( !value ){
          //construct candidate solution
          Trace("cegqi-nsi") << "NSI : refuted current candidate conjecture, construct corresponding solution..." << std::endl;
          d_ns_guard = Node::null();

          std::map< Node, Node > lams;
          for( unsigned i=0; i<d_orig_quant[0].getNumChildren(); i++ ){
            Node prog = d_orig_quant[0][i].getAttribute(SygusSynthFunAttribute());
            int rcons;
            Node sol = getSolution( i, prog.getType(), rcons, false );
            Trace("cegqi-nsi") << "  solution for " << prog << " : " << sol << std::endl;
            //make corresponding lambda
/*
            std::map< Node, Node >::iterator it_nso = d_nsi_op_map.find( prog );
            if( it_nso!=d_nsi_op_map.end() ){
              lams[it_nso->second] = sol;
            }else{
              Assert( false );
            }
*/      
            lams[prog] = sol;
          }

          //now, we will check if this candidate solution satisfies the non-single-invocation portion of the specification
          Node inst = d_sip->getSpecificationInst( 1, lams );
          Trace("cegqi-nsi") << "NSI : specification instantiation : " << inst << std::endl;
          inst = TermDb::simpleNegate( inst );
          std::vector< Node > subs;
          for( unsigned i=0; i<d_sip->d_all_vars.size(); i++ ){
            subs.push_back( NodeManager::currentNM()->mkSkolem( "kv", d_sip->d_all_vars[i].getType(), "created for verifying nsi" ) );
          }
          Assert( d_sip->d_all_vars.size()==subs.size() );
          inst = inst.substitute( d_sip->d_all_vars.begin(), d_sip->d_all_vars.end(), subs.begin(), subs.end() );
          Trace("cegqi-nsi") << "NSI : verification : " << inst << std::endl;
          Trace("cegqi-lemma") << "Cegqi::Lemma : verification lemma : " << inst << std::endl;
          d_qe->addLemma( inst );
          /*
          Node finst = d_sip->getFullSpecification();
          finst = finst.substitute( d_sip->d_all_vars.begin(), d_sip->d_all_vars.end(), subs.begin(), subs.end() );
          Trace("cegqi-nsi") << "NSI : check refinement : " << finst << std::endl;
          Node finst_lem = NodeManager::currentNM()->mkNode( OR, d_full_guard.negate(), finst );
          Trace("cegqi-lemma") << "Cegqi::Lemma : verification, refinement lemma : " << inst << std::endl;
          d_qe->addLemma( finst_lem );
          */
          Trace("cegqi-si-debug") << "CegConjectureSingleInv::check returned verification lemma (nsi)..." << std::endl;
          return true;
        }else{
          //currently trying to construct candidate by refutation (by d_cinst->check below)
        }
      }else{
        //should be assigned a SAT value
        Assert( false );
      }
    }else if( !isFullySingleInvocation() ){
      //create next candidate conjecture
      Assert( d_ei!=NULL );
      //construct d_single_inv
      d_single_inv = Node::null();
      initializeNextSiConjecture();
      Trace("cegqi-si-debug") << "CegConjectureSingleInv::check initialized next si conjecture..." << std::endl;
      return true;
    }
    Trace("cegqi-si-debug") << "CegConjectureSingleInv::check consulting ceg instantiation..." << std::endl;
    d_curr_lemmas.clear();
    Assert( d_cinst!=NULL );
    //call check for instantiator
    d_cinst->check();
    //add lemmas
    //add guard if not fully single invocation
    if( !isFullySingleInvocation() ){
      Assert( !d_ns_guard.isNull() );
      for( unsigned i=0; i<d_curr_lemmas.size(); i++ ){
        lems.push_back( NodeManager::currentNM()->mkNode( OR, d_ns_guard.negate(), d_curr_lemmas[i] ) );
      }
    }else{
      lems.insert( lems.end(), d_curr_lemmas.begin(), d_curr_lemmas.end() );
    }
    return !lems.empty();
  }else{
    // not single invocation
    return false;
  }
}

Node CegConjectureSingleInv::constructSolution( std::vector< unsigned >& indices, unsigned i, unsigned index, std::map< Node, Node >& weak_imp ) {
  Assert( index<d_inst.size() );
  Assert( i<d_inst[index].size() );
  unsigned uindex = indices[index];
  if( index==indices.size()-1 ){
    return d_inst[uindex][i];
  }else{
    Node cond = d_lemmas_produced[uindex];
    //weaken based on unsat core
    std::map< Node, Node >::iterator itw = weak_imp.find( cond );
    if( itw!=weak_imp.end() ){
      cond = itw->second;
    }
    cond = TermDb::simpleNegate( cond );
    Node ite1 = d_inst[uindex][i];
    Node ite2 = constructSolution( indices, i, index+1, weak_imp );
    return NodeManager::currentNM()->mkNode( ITE, cond, ite1, ite2 );
  }
}

//TODO: use term size?
struct sortSiInstanceIndices {
  CegConjectureSingleInv* d_ccsi;
  int d_i;
  bool operator() (unsigned i, unsigned j) {
    if( d_ccsi->d_inst[i][d_i].isConst() && !d_ccsi->d_inst[j][d_i].isConst() ){
      return true;
    }else{
      return false;
    }
  }
};


Node CegConjectureSingleInv::postProcessSolution( Node n ){
  ////remove boolean ITE (not allowed for sygus comp 2015)
  //if( n.getKind()==ITE && n.getType().isBoolean() ){
  //  Node n1 = postProcessSolution( n[1] );
  //  Node n2 = postProcessSolution( n[2] );
  //  return NodeManager::currentNM()->mkNode( OR, NodeManager::currentNM()->mkNode( AND, n[0], n1 ),
  //                                               NodeManager::currentNM()->mkNode( AND, n[0].negate(), n2 ) );
  //}else{
  bool childChanged = false;
  Kind k = n.getKind();
  if( n.getKind()==INTS_DIVISION_TOTAL ){
    k = INTS_DIVISION;
    childChanged = true;
  }else if( n.getKind()==INTS_MODULUS_TOTAL ){
    k = INTS_MODULUS;
    childChanged = true;
  }
  std::vector< Node > children;
  for( unsigned i=0; i<n.getNumChildren(); i++ ){
    Node nn = postProcessSolution( n[i] );
    children.push_back( nn );
    childChanged = childChanged || nn!=n[i];
  }
  if( childChanged ){
    if( n.hasOperator() && k==n.getKind() ){
      children.insert( children.begin(), n.getOperator() );
    }
    return NodeManager::currentNM()->mkNode( k, children );
  }else{
    return n;
  }
  //}
}


Node CegConjectureSingleInv::getSolution( unsigned sol_index, TypeNode stn, int& reconstructed, bool rconsSygus ){
  Assert( d_sol!=NULL );
  Assert( !d_lemmas_produced.empty() );
  const Datatype& dt = ((DatatypeType)(stn).toType()).getDatatype();
  Node varList = Node::fromExpr( dt.getSygusVarList() );
  Node prog = d_orig_quant[0][sol_index].getAttribute(SygusSynthFunAttribute());
  std::vector< Node > vars;
  Node s;
  if( d_prog_to_sol_index.find( prog )==d_prog_to_sol_index.end() ){
    Trace("csi-sol") << "Get solution for (unconstrained) " << prog << std::endl;
    s = d_qe->getTermDatabase()->getEnumerateTerm( TypeNode::fromType( dt.getSygusType() ), 0 );
  }else{
    Trace("csi-sol") << "Get solution for " << prog << ", with skolems : ";
    sol_index = d_prog_to_sol_index[prog];
    d_sol->d_varList.clear();
    Assert( d_single_inv_arg_sk.size()==varList.getNumChildren() );
    for( unsigned i=0; i<d_single_inv_arg_sk.size(); i++ ){
      Trace("csi-sol") << d_single_inv_arg_sk[i] << " ";
      vars.push_back( d_single_inv_arg_sk[i] );
      d_sol->d_varList.push_back( varList[i] );
    }
    Trace("csi-sol") << std::endl;

    //construct the solution
    Trace("csi-sol") << "Sort solution return values " << sol_index << std::endl;
    bool useUnsatCore = false;
    std::vector< Node > active_lemmas;
    //minimize based on unsat core, if possible
    std::map< Node, Node > weak_imp;
    if( options::cegqiSolMinCore() ){
      if( options::cegqiSolMinInst() ){
        if( d_qe->getUnsatCoreLemmas( active_lemmas, weak_imp ) ){
          useUnsatCore = true;
        }
      }else{
        if( d_qe->getUnsatCoreLemmas( active_lemmas ) ){
          useUnsatCore = true;
        }
      }
    } 
    Assert( d_lemmas_produced.size()==d_inst.size() );
    std::vector< unsigned > indices;
    for( unsigned i=0; i<d_lemmas_produced.size(); i++ ){
      bool incl = true;
      if( useUnsatCore ){
        incl = std::find( active_lemmas.begin(), active_lemmas.end(), d_lemmas_produced[i] )!=active_lemmas.end();
      }
      if( incl ){
        Assert( sol_index<d_inst[i].size() );
        indices.push_back( i );
      }
    }
    Trace("csi-sol") << "...included " << indices.size() << " / " << d_lemmas_produced.size() << " instantiations." << std::endl;
    Assert( !indices.empty() );
    //sort indices based on heuristic : currently, do all constant returns first (leads to simpler conditions)
    // TODO : to minimize solution size, put the largest term last
    sortSiInstanceIndices ssii;
    ssii.d_ccsi = this;
    ssii.d_i = sol_index;
    std::sort( indices.begin(), indices.end(), ssii );
    Trace("csi-sol") << "Construct solution" << std::endl;
    s = constructSolution( indices, sol_index, 0, weak_imp );
    Assert( vars.size()==d_sol->d_varList.size() );
    s = s.substitute( vars.begin(), vars.end(), d_sol->d_varList.begin(), d_sol->d_varList.end() );
  }
  d_orig_solution = s;

  //simplify the solution
  Trace("csi-sol") << "Solution (pre-simplification): " << d_orig_solution << std::endl;
  s = d_sol->simplifySolution( s, stn );
  Trace("csi-sol") << "Solution (post-simplification): " << s << std::endl;
  return reconstructToSyntax( s, stn, reconstructed, rconsSygus );
}

Node CegConjectureSingleInv::reconstructToSyntax( Node s, TypeNode stn, int& reconstructed, bool rconsSygus ) {
  d_solution = s;
  const Datatype& dt = ((DatatypeType)(stn).toType()).getDatatype();

  //reconstruct the solution into sygus if necessary
  reconstructed = 0;
  if( options::cegqiSingleInvReconstruct() && !dt.getSygusAllowAll() && !stn.isNull() && rconsSygus ){
    d_sol->preregisterConjecture( d_orig_conjecture );
    d_sygus_solution = d_sol->reconstructSolution( s, stn, reconstructed );
    if( reconstructed==1 ){
      Trace("csi-sol") << "Solution (post-reconstruction into Sygus): " << d_sygus_solution << std::endl;
    }
  }else{
    Trace("csi-sol") << "Post-process solution..." << std::endl;
    Node prev = d_solution;
    d_solution = postProcessSolution( d_solution );
    if( prev!=d_solution ){
      Trace("csi-sol") << "Solution (after post process) : " << d_solution << std::endl;
    }
  }


  if( Trace.isOn("csi-sol") ){
    //debug solution
    if( !d_sol->debugSolution( d_solution ) ){
      Trace("csi-sol") << "WARNING : solution " << d_solution << " contains free constants." << std::endl;
      //exit( 47 );
    }else{
      //exit( 49 );
    }
  }
  if( Trace.isOn("cegqi-stats") ){
    int tsize, itesize;
    tsize = 0;itesize = 0;
    d_sol->debugTermSize( d_orig_solution, tsize, itesize );
    Trace("cegqi-stats") << tsize << " " << itesize << " ";
    tsize = 0;itesize = 0;
    d_sol->debugTermSize( d_solution, tsize, itesize );
    Trace("cegqi-stats") << tsize << " " << itesize << " ";
    if( !d_sygus_solution.isNull() ){
      tsize = 0;itesize = 0;
      d_sol->debugTermSize( d_sygus_solution, tsize, itesize );
      Trace("cegqi-stats") << tsize << " - ";
    }else{
      Trace("cegqi-stats") << "null ";
    }
    Trace("cegqi-stats") << std::endl;
  }
  Node sol;
  if( reconstructed==1 ){
    sol = d_sygus_solution;
  }else if( reconstructed==-1 ){
    return Node::null();
  }else{
    sol = d_solution;
  }
  //make into lambda
  if( !dt.getSygusVarList().isNull() ){
    Node varList = Node::fromExpr( dt.getSygusVarList() );
    return NodeManager::currentNM()->mkNode( LAMBDA, varList, sol );
  }else{
    return sol;
  }
}

bool CegConjectureSingleInv::needsCheck() {
  if( options::cegqiSingleInvMode()==CEGQI_SI_MODE_ALL_ABORT ){
    if( !d_has_ites ){
      return d_inst.empty();
    }
  }
  return true;
}

void CegConjectureSingleInv::preregisterConjecture( Node q ) {
  d_orig_conjecture = q;
}

bool SingleInvocationPartition::init( Node n ) {
  //first, get types of arguments for functions
  std::vector< TypeNode > typs;
  std::map< Node, bool > visited;
  std::vector< Node > funcs;
  if( inferArgTypes( n, typs, visited ) ){
    return init( funcs, typs, n, false );
  }else{
    Trace("si-prt") << "Could not infer argument types." << std::endl;
    return false;
  }
}

// gets the argument type list for the first APPLY_UF we see
bool SingleInvocationPartition::inferArgTypes( Node n, std::vector< TypeNode >& typs, std::map< Node, bool >& visited ) {
  if( visited.find( n )==visited.end() ){
    visited[n] = true;
    if( n.getKind()!=FORALL ){
      if( n.getKind()==APPLY_UF ){
        for( unsigned i=0; i<n.getNumChildren(); i++ ){
          typs.push_back( n[i].getType() );
        }
        return true;
      }else{
        for( unsigned i=0; i<n.getNumChildren(); i++ ){
          if( inferArgTypes( n[i], typs, visited ) ){
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool SingleInvocationPartition::init( std::vector< Node >& funcs, Node n ) {
  Trace("si-prt") << "Initialize with " << funcs.size() << " input functions..." << std::endl;
  std::vector< TypeNode > typs;
  if( !funcs.empty() ){
    TypeNode tn0 = funcs[0].getType();
    for( unsigned i=1; i<funcs.size(); i++ ){
      if( funcs[i].getType()!=tn0 ){
        // can't anti-skolemize functions of different sort
        Trace("si-prt") << "...type mismatch" << std::endl;
        return false;
      }
    }
    if( tn0.getNumChildren()>1 ){
      for( unsigned j=0; j<tn0.getNumChildren()-1; j++ ){
        typs.push_back( tn0[j] );
      }
    }
  }
  Trace("si-prt") << "#types = " << typs.size() << std::endl;
  return init( funcs, typs, n, true );  
}

bool SingleInvocationPartition::init( std::vector< Node >& funcs, std::vector< TypeNode >& typs, Node n, bool has_funcs ){
  Assert( d_arg_types.empty() );
  Assert( d_input_funcs.empty() );
  Assert( d_si_vars.empty() );
  d_has_input_funcs = has_funcs;
  d_arg_types.insert( d_arg_types.end(), typs.begin(), typs.end() );
  d_input_funcs.insert( d_input_funcs.end(), funcs.begin(), funcs.end() );
  Trace("si-prt") << "Initialize..." << std::endl;
  for( unsigned j=0; j<d_arg_types.size(); j++ ){
    std::stringstream ss;
    ss << "s_" << j;
    Node si_v = NodeManager::currentNM()->mkBoundVar( ss.str(), d_arg_types[j] );
    d_si_vars.push_back( si_v );
  }
  Trace("si-prt") << "Process the formula..." << std::endl;
  process( n );
  return true;
}


void SingleInvocationPartition::process( Node n ) {
  Assert( d_si_vars.size()==d_arg_types.size() );
  Trace("si-prt") << "SingleInvocationPartition::process " << n << std::endl;
  Trace("si-prt") << "Get conjuncts..." << std::endl;
  std::vector< Node > conj;
  if( collectConjuncts( n, true, conj ) ){
    Trace("si-prt") << "...success." << std::endl;
    for( unsigned i=0; i<conj.size(); i++ ){
      std::vector< Node > si_terms;
      std::vector< Node > si_subs;
      Trace("si-prt") << "Process conjunct : " << conj[i] << std::endl;
      //do DER on conjunct
      Node cr = TermDb::getQuantSimplify( conj[i] );
      if( cr!=conj[i] ){
        Trace("si-prt-debug") << "...rewritten to " << cr << std::endl;
      }
      std::map< Node, bool > visited;
      // functions to arguments
      std::vector< Node > args;
      std::vector< Node > terms;
      std::vector< Node > subs;
      bool singleInvocation = true;
      bool ngroundSingleInvocation = false;
      if( processConjunct( cr, visited, args, terms, subs ) ){
        for( unsigned j=0; j<terms.size(); j++ ){
          si_terms.push_back( subs[j] );
          Node op = subs[j].hasOperator() ? subs[j].getOperator() : subs[j];
          Assert( d_func_fo_var.find( op )!=d_func_fo_var.end() );
          si_subs.push_back( d_func_fo_var[op] );
        }
        std::map< Node, Node > subs_map;
        std::map< Node, Node > subs_map_rev;
        std::vector< Node > funcs;
        //normalize the invocations
        if( !terms.empty() ){
          Assert( terms.size()==subs.size() );
          cr = cr.substitute( terms.begin(), terms.end(), subs.begin(), subs.end() );
        }
        std::vector< Node > children;
        children.push_back( cr );
        terms.clear();
        subs.clear();
        Trace("si-prt") << "...single invocation, with arguments: " << std::endl;
        for( unsigned j=0; j<args.size(); j++ ){
          Trace("si-prt") << args[j] << " ";
          if( args[j].getKind()==BOUND_VARIABLE && std::find( terms.begin(), terms.end(), args[j] )==terms.end() ){
            terms.push_back( args[j] );
            subs.push_back( d_si_vars[j] );
          }else{
            children.push_back( d_si_vars[j].eqNode( args[j] ).negate() );
          }
        }
        Trace("si-prt") << std::endl;
        cr = children.size()==1 ? children[0] : NodeManager::currentNM()->mkNode( OR, children );
        Assert( terms.size()==subs.size() );
        cr = cr.substitute( terms.begin(), terms.end(), subs.begin(), subs.end() );
        Trace("si-prt-debug") << "...normalized invocations to " << cr << std::endl;
        //now must check if it has other bound variables
        std::vector< Node > bvs;
        TermDb::getBoundVars( cr, bvs );
        if( bvs.size()>d_si_vars.size() ){
          Trace("si-prt") << "...not ground single invocation." << std::endl;
          ngroundSingleInvocation = true;
          singleInvocation = false;
        }else{
          Trace("si-prt") << "...ground single invocation : success." << std::endl;
        }
      }else{
        Trace("si-prt") << "...not single invocation." << std::endl;
        singleInvocation = false;
        //rename bound variables with maximal overlap with si_vars
        std::vector< Node > bvs;
        TermDb::getBoundVars( cr, bvs );
        std::vector< Node > terms;
        std::vector< Node > subs;
        for( unsigned j=0; j<bvs.size(); j++ ){
          TypeNode tn = bvs[j].getType();
          Trace("si-prt-debug") << "Fit bound var #" << j << " : " << bvs[j] << " with si." << std::endl;
          for( unsigned k=0; k<d_si_vars.size(); k++ ){
            if( tn==d_arg_types[k] ){
              if( std::find( subs.begin(), subs.end(), d_si_vars[k] )==subs.end() ){
                terms.push_back( bvs[j] );
                subs.push_back( d_si_vars[k] );
                Trace("si-prt-debug") << "  ...use " << d_si_vars[k] << std::endl;
                break;
              }
            }
          }
        }
        Assert( terms.size()==subs.size() );
        cr = cr.substitute( terms.begin(), terms.end(), subs.begin(), subs.end() );
      }
      cr = Rewriter::rewrite( cr );
      Trace("si-prt") << ".....got si=" << singleInvocation << ", result : " << cr << std::endl;
      d_conjuncts[2].push_back( cr );
      TermDb::getBoundVars( cr, d_all_vars );
      if( singleInvocation ){
        //replace with single invocation formulation
        Assert( si_terms.size()==si_subs.size() );
        cr = cr.substitute( si_terms.begin(), si_terms.end(), si_subs.begin(), si_subs.end() );
        cr = Rewriter::rewrite( cr );
        Trace("si-prt") << ".....si version=" << cr << std::endl;
        d_conjuncts[0].push_back( cr );
      }else{
        d_conjuncts[1].push_back( cr );
        if( ngroundSingleInvocation ){
          d_conjuncts[3].push_back( cr );
        }
      }
    }
  }else{
    Trace("si-prt") << "...failed." << std::endl;
  }
}

bool SingleInvocationPartition::collectConjuncts( Node n, bool pol, std::vector< Node >& conj ) {
  if( ( !pol && n.getKind()==OR ) || ( pol && n.getKind()==AND ) ){
    for( unsigned i=0; i<n.getNumChildren(); i++ ){
      if( !collectConjuncts( n[i], pol, conj ) ){
        return false;
      }
    }
  }else if( n.getKind()==NOT ){
    return collectConjuncts( n[0], !pol, conj );
  }else if( n.getKind()==FORALL ){
    return false;
  }else{
    if( !pol ){
      n = TermDb::simpleNegate( n );
    }
    Trace("si-prt") << "Conjunct : " << n << std::endl;
    conj.push_back( n );
  }
  return true;
}

bool SingleInvocationPartition::processConjunct( Node n, std::map< Node, bool >& visited, std::vector< Node >& args,
                                                 std::vector< Node >& terms, std::vector< Node >& subs ) {
  std::map< Node, bool >::iterator it = visited.find( n );
  if( it!=visited.end() ){
    return true;
  }else{
    bool ret = true;
    //if( TermDb::hasBoundVarAttr( n ) ){
      for( unsigned i=0; i<n.getNumChildren(); i++ ){
        if( !processConjunct( n[i], visited, args, terms, subs ) ){
          ret = false;
        }
      }
      if( ret ){
        Node f;
        bool success = false;
        if( d_has_input_funcs ){
          f = n.hasOperator() ? n.getOperator() : n;
          if( std::find( d_input_funcs.begin(), d_input_funcs.end(), f )!=d_input_funcs.end() ){
            success = true;
          }
        }else{
          if( n.getKind()==kind::APPLY_UF ){
            f = n.getOperator();
            success = true;
          }
        }
        if( success ){
          if( std::find( terms.begin(), terms.end(), n )==terms.end() ){
            //check if it matches the type requirement
            if( isAntiSkolemizableType( f ) ){
              if( args.empty() ){
                //record arguments
                for( unsigned i=0; i<n.getNumChildren(); i++ ){
                  args.push_back( n[i] );
                }
              }else{
                //arguments must be the same as those already recorded
                for( unsigned i=0; i<n.getNumChildren(); i++ ){
                  if( args[i]!=n[i] ){
                    Trace("si-prt-debug") << "...bad invocation : " << n << " at arg " << i << "." << std::endl;
                    ret = false;
                    break;
                  }
                }
              }
              if( ret ){
                terms.push_back( n );
                subs.push_back( d_func_inv[f] );
              }
            }else{
              Trace("si-prt-debug") << "... " << f << " is a bad operator." << std::endl;
              ret = false;
            }
          }
        }
      }
    //}
    visited[n] = ret;
    return ret;
  }
}

bool SingleInvocationPartition::isAntiSkolemizableType( Node f ) {
  std::map< Node, bool >::iterator it = d_funcs.find( f );
  if( it!=d_funcs.end() ){
    return it->second;
  }else{
    TypeNode tn = f.getType();
    bool ret = false;
    if( tn.getNumChildren()==d_arg_types.size()+1 || ( d_arg_types.empty() && tn.getNumChildren()==0 ) ){
      ret = true;
      std::vector< Node > children;
      children.push_back( f );
      //TODO: permutations of arguments
      for( unsigned i=0; i<d_arg_types.size(); i++ ){
        children.push_back( d_si_vars[i] );
        if( tn[i]!=d_arg_types[i] ){
          ret = false;
          break;
        }
      }
      if( ret ){
        Node t;
        if( children.size()>1 ){
          t = NodeManager::currentNM()->mkNode( kind::APPLY_UF, children );
        }else{
          t = children[0];
        }
        d_func_inv[f] = t;
        d_inv_to_func[t] = f;
        std::stringstream ss;
        ss << "F_" << f;
        TypeNode rt;
        if( d_arg_types.empty() ){
          rt = tn;
        }else{
          rt = tn.getRangeType();
        }
        Node v = NodeManager::currentNM()->mkBoundVar( ss.str(), rt );
        d_func_fo_var[f] = v;
        d_fo_var_to_func[v] = f;
        d_func_vars.push_back( v );
      }
    }
    d_funcs[f] = ret;
    return ret;
  }
}

Node SingleInvocationPartition::getConjunct( int index ) {
  return d_conjuncts[index].empty() ? NodeManager::currentNM()->mkConst( true ) :
          ( d_conjuncts[index].size()==1 ? d_conjuncts[index][0] : NodeManager::currentNM()->mkNode( AND, d_conjuncts[index] ) );
}

Node SingleInvocationPartition::getSpecificationInst( Node n, std::map< Node, Node >& lam, std::map< Node, Node >& visited ) {
  std::map< Node, Node >::iterator it = visited.find( n );
  if( it!=visited.end() ){
    return it->second;
  }else{
    bool childChanged = false;
    std::vector< Node > children;
    for( unsigned i=0; i<n.getNumChildren(); i++ ){
      Node nn = getSpecificationInst( n[i], lam, visited );
      children.push_back( nn );
      childChanged = childChanged || ( nn!=n[i] );
    }
    Node ret;
    Node f;
    bool success = false;
    if( d_has_input_funcs ){
      f = n.hasOperator() ? n.getOperator() : n;
      if( std::find( d_input_funcs.begin(), d_input_funcs.end(), f )!=d_input_funcs.end() ){
        success = true;
      }
    }else{
      if( n.getKind()==APPLY_UF ){
        f = n.getOperator();
        success = true;
      }
    }
    if( success ){
      std::map< Node, Node >::iterator itl = lam.find( f );
      if( itl!=lam.end() ){
        Assert( itl->second[0].getNumChildren()==children.size() );
        std::vector< Node > terms;
        std::vector< Node > subs;
        for( unsigned i=0; i<itl->second[0].getNumChildren(); i++ ){
          terms.push_back( itl->second[0][i] );
          subs.push_back( children[i] );
        }
        ret = itl->second[1].substitute( terms.begin(), terms.end(), subs.begin(), subs.end() );
        ret = Rewriter::rewrite( ret );
      }
    }
    if( ret.isNull() ){
      ret = n;
      if( childChanged ){
        if( n.getMetaKind() == kind::metakind::PARAMETERIZED ){
          children.insert( children.begin(), n.getOperator() );
        }
        ret = NodeManager::currentNM()->mkNode( n.getKind(), children );
      }
    }
    return ret;
  }
}

Node SingleInvocationPartition::getSpecificationInst( int index, std::map< Node, Node >& lam ) {
  Node conj = getConjunct( index );
  std::map< Node, Node > visited;
  return getSpecificationInst( conj, lam, visited );
}

void SingleInvocationPartition::debugPrint( const char * c ) {
  Trace(c) << "Single invocation variables : ";
  for( unsigned i=0; i<d_si_vars.size(); i++ ){
    Trace(c) << d_si_vars[i] << " ";
  }
  Trace(c) << std::endl;
  Trace(c) << "Functions : " << std::endl;
  for( std::map< Node, bool >::iterator it = d_funcs.begin(); it != d_funcs.end(); ++it ){
    Trace(c) << "  " << it->first << " : ";
    if( it->second ){
      Trace(c) << d_func_inv[it->first] << " " << d_func_fo_var[it->first] << std::endl;
    }else{
      Trace(c) << "not incorporated." << std::endl;
    }
  }
  for( unsigned i=0; i<4; i++ ){
    Trace(c) << ( i==0 ? "Single invocation" : ( i==1 ? "Non-single invocation" : ( i==2 ? "All" : "Non-ground single invocation" ) ) );
    Trace(c) << " conjuncts: " << std::endl;
    for( unsigned j=0; j<d_conjuncts[i].size(); j++ ){
      Trace(c) << "  " << (j+1) << " : " << d_conjuncts[i][j] << std::endl;
    }
  }
  Trace(c) << std::endl;
}


bool DetTrace::DetTraceTrie::add( Node loc, std::vector< Node >& val, unsigned index ){
  if( index==val.size() ){
    if( d_children.empty() ){
      d_children[loc].clear();
      return true;
    }else{
      return false;
    }
  }else{
    return d_children[val[index]].add( loc, val, index+1 );
  }
}

Node DetTrace::DetTraceTrie::constructFormula( std::vector< Node >& vars, unsigned index ){
  if( index==vars.size() ){
    return NodeManager::currentNM()->mkConst( true );    
  }else{
    std::vector< Node > disj;
    for( std::map< Node, DetTraceTrie >::iterator it = d_children.begin(); it != d_children.end(); ++it ){
      Node eq = vars[index].eqNode( it->first );
      if( index<vars.size()-1 ){
        Node conc = it->second.constructFormula( vars, index+1 );
        disj.push_back( NodeManager::currentNM()->mkNode( kind::AND, eq, conc ) );
      }else{
        disj.push_back( eq );
      }
    }
    Assert( !disj.empty() );
    return disj.size()==1 ? disj[0] : NodeManager::currentNM()->mkNode( kind::OR, disj );
  }
}

bool DetTrace::increment( Node loc, std::vector< Node >& vals ){
  if( d_trie.add( loc, vals ) ){
    for( unsigned i=0; i<vals.size(); i++ ){
      d_curr[i] = vals[i];
    }
    return true;
  }else{
    return false;
  }
}

Node DetTrace::constructFormula( std::vector< Node >& vars ) {
  return d_trie.constructFormula( vars );
}


void DetTrace::print( const char* c ) {
  for( unsigned i=0; i<d_curr.size(); i++ ){
    Trace(c) << d_curr[i] << " ";
  }
}

void TransitionInference::initialize( Node f, std::vector< Node >& vars ) {
  Assert( d_vars.empty() );
  d_func = f;
  d_vars.insert( d_vars.end(), vars.begin(), vars.end() );
}


void TransitionInference::getConstantSubstitution( std::vector< Node >& vars, std::vector< Node >& disjuncts, std::vector< Node >& const_var, std::vector< Node >& const_subs, bool reqPol ) {
  for( unsigned j=0; j<disjuncts.size(); j++ ){
    Node sn;
    if( !const_var.empty() ){
      sn = disjuncts[j].substitute( const_var.begin(), const_var.end(), const_subs.begin(), const_subs.end() );
      sn = Rewriter::rewrite( sn );
    }else{
      sn = disjuncts[j];
    }
    bool slit_pol = sn.getKind()!=NOT;
    Node slit = sn.getKind()==NOT ? sn[0] : sn;
    if( slit.getKind()==EQUAL && slit_pol==reqPol ){
      // check if it is a variable equality
      TNode v;
      Node s;
      for( unsigned r=0; r<2; r++ ){
        if( std::find( vars.begin(), vars.end(), slit[r] )!=vars.end() ){
          if( !TermDb::containsTerm( slit[1-r], slit[r] ) ){
            v = slit[r];
            s = slit[1-r];
            break;
          }
        }
      }
      if( v.isNull() ){
        //solve for var
        std::map< Node, Node > msum;
        if( QuantArith::getMonomialSumLit( slit, msum ) ){
          for( std::map< Node, Node >::iterator itm = msum.begin(); itm != msum.end(); ++itm ){
            if( std::find( vars.begin(), vars.end(), itm->first )!=vars.end() ){  
              Node veq_c;
              Node val;
              int ires = QuantArith::isolate( itm->first, msum, veq_c, val, EQUAL );
              if( ires!=0 && veq_c.isNull() && !TermDb::containsTerm( val, itm->first ) ){
                v = itm->first;
                s = val;
              }
            }
          }
        }
      }
      if( !v.isNull() ){
        TNode ts = s;
        for( unsigned k=0; k<const_subs.size(); k++ ){
          const_subs[k] = Rewriter::rewrite( const_subs[k].substitute( v, ts ) );
        }
        Trace("cegqi-inv-debug2") << "...substitution : " << v << " -> " << s << std::endl;
        const_var.push_back( v );
        const_subs.push_back( s );
      }
    }
  }
}

void TransitionInference::process( Node n ) {
  d_complete = true;
  std::vector< Node > n_check;
  if( n.getKind()==AND ){
    for( unsigned i=0; i<n.getNumChildren(); i++ ){
      n_check.push_back( n[i] );
    }
  }else{
    n_check.push_back( n );
  }
  for( unsigned i=0; i<n_check.size(); i++ ){
    Node nn = n_check[i];
    std::map< Node, bool > visited;
    std::map< bool, Node > terms;
    std::vector< Node > disjuncts;
    Trace("cegqi-inv") << "TransitionInference : Process disjunct : " << nn << std::endl;
    if( processDisjunct( nn, terms, disjuncts, visited, true ) ){
      if( !terms.empty() ){
        Node norm_args;
        int comp_num;
        std::map< bool, Node >::iterator itt = terms.find( false );
        if( itt!=terms.end() ){
          norm_args = itt->second;
          if( terms.find( true )!=terms.end() ){
            comp_num = 0;
          }else{
            comp_num = -1;
          }
        }else{
          norm_args = terms[true];
          comp_num = 1;
        }
        std::vector< Node > subs;
        for( unsigned j=0; j<norm_args.getNumChildren(); j++ ){
          subs.push_back( norm_args[j] );
        }        
        Trace("cegqi-inv-debug2") << "  normalize based on " << norm_args << std::endl;
        Assert( d_vars.size()==subs.size() );
        for( unsigned j=0; j<disjuncts.size(); j++ ){
          disjuncts[j] = Rewriter::rewrite( disjuncts[j].substitute( subs.begin(), subs.end(), d_vars.begin(), d_vars.end() ) );
          Trace("cegqi-inv-debug2") << "  ..." << disjuncts[j] << std::endl;
        }
        std::vector< Node > const_var;
        std::vector< Node > const_subs;
        if( comp_num==0 ){
          //transition
          Assert( terms.find( true )!=terms.end() );
          Node next = terms[true];
          next = Rewriter::rewrite( next.substitute( subs.begin(), subs.end(), d_vars.begin(), d_vars.end() ) );
          Trace("cegqi-inv-debug") << "transition next predicate : " << next << std::endl;
          // normalize the other direction
          std::vector< Node > rvars;
          for( unsigned i=0; i<next.getNumChildren(); i++ ){
            rvars.push_back( next[i] );
          }
          if( d_prime_vars.size()<next.getNumChildren() ){
            for( unsigned i=0; i<next.getNumChildren(); i++ ){
              Node v = NodeManager::currentNM()->mkSkolem( "ir", next[i].getType(), "template inference rev argument" );
              d_prime_vars.push_back( v );
            }
          }
          Trace("cegqi-inv-debug2") << "  normalize based on " << next << std::endl;
          Assert( d_vars.size()==subs.size() );
          for( unsigned j=0; j<disjuncts.size(); j++ ){
            disjuncts[j] = Rewriter::rewrite( disjuncts[j].substitute( rvars.begin(), rvars.end(), d_prime_vars.begin(), d_prime_vars.end() ) );
            Trace("cegqi-inv-debug2") << "  ..." << disjuncts[j] << std::endl;
          }
          getConstantSubstitution( d_prime_vars, disjuncts, const_var, const_subs, false );
        }else{
          getConstantSubstitution( d_vars, disjuncts, const_var, const_subs, false );
        }
        Node res;
        if( disjuncts.empty() ){
          res = NodeManager::currentNM()->mkConst( false );
        }else if( disjuncts.size()==1 ){
          res = disjuncts[0];
        }else{
          res = NodeManager::currentNM()->mkNode( kind::OR, disjuncts );
        }
        if( !res.hasBoundVar() ){
          Trace("cegqi-inv") << "*** inferred " << ( comp_num==1 ? "pre" : ( comp_num==-1 ? "post" : "trans" ) ) << "-condition : " << res << std::endl;
          d_com[comp_num].d_conjuncts.push_back( res );
          if( !const_var.empty() ){
            bool has_const_eq = const_var.size()==d_vars.size();
            Trace("cegqi-inv") << "    with constant substitution, complete = " << has_const_eq << " : " << std::endl;
            for( unsigned i=0; i<const_var.size(); i++ ){
              Trace("cegqi-inv") << "      " << const_var[i] << " -> " << const_subs[i] << std::endl;
              if( has_const_eq ){
                d_com[comp_num].d_const_eq[res][const_var[i]] = const_subs[i];
              }
            }
            Trace("cegqi-inv") << "...size = " << const_var.size() << ", #vars = " << d_vars.size() << std::endl;
          }
        }else{
          Trace("cegqi-inv-debug2") << "...failed, free variable." << std::endl;
          d_complete = false;
        }
      }
    }else{
      d_complete = false;
    }
  }
  
  // finalize the components
  for( int i=-1; i<=1; i++ ){
    Node ret;
    if( d_com[i].d_conjuncts.empty() ){
      ret = NodeManager::currentNM()->mkConst( true );
    }else if( d_com[i].d_conjuncts.size()==1 ){
      ret = d_com[i].d_conjuncts[0];
    }else{
      ret = NodeManager::currentNM()->mkNode( kind::AND, d_com[i].d_conjuncts );
    }
    if( i==0 || i==1 ){
      // pre-condition and transition are negated
      ret = TermDb::simpleNegate( ret );
    }
    d_com[i].d_this = ret;
  }
}

bool TransitionInference::processDisjunct( Node n, std::map< bool, Node >& terms, std::vector< Node >& disjuncts, 
                                           std::map< Node, bool >& visited, bool topLevel ) {
  if( visited.find( n )==visited.end() ){
    visited[n] = true;
    bool childTopLevel = n.getKind()==OR && topLevel;
    //if another part mentions UF or a free variable, then fail
    bool lit_pol = n.getKind()!=NOT;
    Node lit = n.getKind()==NOT ? n[0] : n;
    if( lit.getKind()==APPLY_UF ){
      Node op = lit.getOperator();
      if( d_func.isNull() ){
        d_func = op;
        Trace("cegqi-inv-debug") << "Use " << op << " with args ";
        for( unsigned i=0; i<lit.getNumChildren(); i++ ){
          Node v = NodeManager::currentNM()->mkSkolem( "i", lit[i].getType(), "template inference argument" );
          d_vars.push_back( v );
          Trace("cegqi-inv-debug") << v << " ";
        }
        Trace("cegqi-inv-debug") << std::endl;
      }
      if( op!=d_func ){
        Trace("cegqi-inv-debug") << "...failed, free function : " << n << std::endl;
        return false;
      }else if( topLevel ){
        if( terms.find( lit_pol )==terms.end() ){
          terms[lit_pol] = lit;
          return true;
        }else{
          Trace("cegqi-inv-debug") << "...failed, repeated inv-app : " << lit << std::endl;
          return false;
        }
      }else{
        Trace("cegqi-inv-debug") << "...failed, non-entailed inv-app : " << lit << std::endl;
        return false;
      }
    }else if( topLevel && !childTopLevel ){
      disjuncts.push_back( n );
    }
    for( unsigned i=0; i<n.getNumChildren(); i++ ){
      if( !processDisjunct( n[i], terms, disjuncts, visited, childTopLevel ) ){
        return false;
      }
    }
  }
  return true;
}

Node TransitionInference::getComponent( int i ) {
  return d_com[i].d_this;
}

int TransitionInference::initializeTrace( DetTrace& dt, Node loc, bool fwd ) {
  int index = fwd ? 1 : -1;
  Assert( d_com[index].has( loc ) );
  std::map< Node, std::map< Node, Node > >::iterator it = d_com[index].d_const_eq.find( loc );
  if( it!=d_com[index].d_const_eq.end() ){
    std::vector< Node > next;
    for( unsigned i=0; i<d_vars.size(); i++ ){
      Node v = d_vars[i];
      Assert( it->second.find( v )!=it->second.end() );
      next.push_back( it->second[v] );
      dt.d_curr.push_back( it->second[v] );
    }
    Trace("cegqi-inv-debug2") << "dtrace : initial increment" << std::endl;
    bool ret = dt.increment( loc, next );
    AlwaysAssert( ret );
    return 0;
  }
  return -1;
}
  
int TransitionInference::incrementTrace( DetTrace& dt, Node loc, bool fwd ) {
  Assert( d_com[0].has( loc ) );
  // check if it satisfies the pre/post condition
  int check_index = fwd ? -1 : 1;
  Node cc = getComponent( check_index );
  Assert( !cc.isNull() );
  Node ccr = Rewriter::rewrite( cc.substitute( d_vars.begin(), d_vars.end(), dt.d_curr.begin(), dt.d_curr.end() ) );
  if( ccr.isConst() ){
    if( ccr.getConst<bool>()==( fwd ? false : true ) ){
      Trace("cegqi-inv-debug2") << "dtrace : counterexample" << std::endl;
      return 2;
    }
  }


  // terminates?
  Node c = getComponent( 0 );
  Assert( !c.isNull() );

  Assert( d_vars.size()==dt.d_curr.size() );
  Node cr = Rewriter::rewrite( c.substitute( d_vars.begin(), d_vars.end(), dt.d_curr.begin(), dt.d_curr.end() ) );
  if( cr.isConst() ){
    if( !cr.getConst<bool>() ){
      Trace("cegqi-inv-debug2") << "dtrace : terminated" << std::endl;
      return 1;
    }else{
      return -1;
    }
  }
  if( fwd ){
    std::map< Node, std::map< Node, Node > >::iterator it = d_com[0].d_const_eq.find( loc );
    if( it!=d_com[0].d_const_eq.end() ){
      std::vector< Node > next;
      for( unsigned i=0; i<d_prime_vars.size(); i++ ){
        Node pv = d_prime_vars[i];
        Assert( it->second.find( pv )!=it->second.end() );
        Node pvs = it->second[pv];
        Assert( d_vars.size()==dt.d_curr.size() );
        Node pvsr = Rewriter::rewrite( pvs.substitute( d_vars.begin(), d_vars.end(), dt.d_curr.begin(), dt.d_curr.end() ) );
        next.push_back( pvsr );
      }
      if( dt.increment( loc, next ) ){
        Trace("cegqi-inv-debug2") << "dtrace : success increment" << std::endl;
        return 0;
      }else{
        // looped
        Trace("cegqi-inv-debug2") << "dtrace : looped" << std::endl;
        return 1;
      }
    }
  }else{
    //TODO
  }
  return -1;
}

int TransitionInference::initializeTrace( DetTrace& dt, bool fwd ) {
  Trace("cegqi-inv-debug2") << "Initialize trace" << std::endl;
  int index = fwd ? 1 : -1;
  if( d_com[index].d_conjuncts.size()==1 ){
    return initializeTrace( dt, d_com[index].d_conjuncts[0], fwd );
  }else{
    return -1;
  }
}

int TransitionInference::incrementTrace( DetTrace& dt, bool fwd ) {
  if( d_com[0].d_conjuncts.size()==1 ){
    return incrementTrace( dt, d_com[0].d_conjuncts[0], fwd );
  }else{
    return -1;
  }
}

Node TransitionInference::constructFormulaTrace( DetTrace& dt ) {
  return dt.constructFormula( d_vars );
}
  
} //namespace CVC4

