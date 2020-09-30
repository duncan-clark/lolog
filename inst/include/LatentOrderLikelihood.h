#ifndef LATENTORDERLIKELIHOOD_H_
#define LATENTORDERLIKELIHOOD_H_

#include "Model.h"
#include "ShallowCopyable.h"
#include "Ranker.h"

#include <cmath>
#include <Rcpp.h>
#include <assert.h>
#include <vector>
#include <iterator>
#include <boost/shared_ptr.hpp>

namespace lolog{


struct IdxCompare
{
  const std::vector<int>& target;
  
  IdxCompare(const std::vector<int>& target): target(target) {}
  
  bool operator()(int a, int b) const { return target[a] < target[b]; }
};

template<class Engine>
class LatentOrderLikelihood : public ShallowCopyable{
protected:
  typedef boost::shared_ptr< Model<Engine> > ModelPtr;
  typedef boost::shared_ptr< std::vector<int> > VectorPtr;
  
  /**
   * The likelihood model with the observed graph
   */
  ModelPtr model;
  
  /**
   * The likelihood model with an empty graph
   */
  ModelPtr noTieModel;
  
  /**
   * A vector giving the (partial) ordering for vertex inclusion
   */
  //VectorPtr order;
  
  /**
   * Fisher-Yates shuffle of elements up to offset
   */
  template<class T>
  void shuffle(std::vector<T>& vec, long offset){
    for( int i=0; i < offset - 1.0; i++){
      //long ind = floor(Rf_runif(0.0,1.0)*offset);
      long ind = floor(Rf_runif(i,offset));
      T tmp = vec[i];
      vec[i] = vec[ind];
      vec[ind] = tmp;
    }
  }
  
  /**
   * Generates a vertex ordering 'vertexOrder' conditional upon a possibly
   * partial ordering 'order'.
   */
  void generateOrder(std::vector<int>& vertexOrder,const VectorPtr order){
    vertexOrder.resize(order->size());
    std::vector<int> y(vertexOrder.size());
    //get ranks. ties broken randomly
    rank(*order, y, "random");
    
    //get ordered indices of ranks
    for(int i=0;i<y.size();i++)
      vertexOrder[i] = i;
    std::sort(  vertexOrder.begin(),
                vertexOrder.end(), IdxCompare(y));
    
  }
  
  
  void removeEdges(ModelPtr mod){
    mod->network()->emptyGraph();
  }
public:
  
  LatentOrderLikelihood(){}
  
  LatentOrderLikelihood(Model<Engine> mod){
    model = mod.clone();
    noTieModel = mod.clone();
    noTieModel->setNetwork(mod.network()->clone());
    removeEdges(noTieModel);
    if(model->hasVertexOrder() && model->getVertexOrder()->size() != model->network()->size())
      Rf_error("Vertex ordering does not have the same number of elements as there are vertices in the network 95.");
  }
  
  /*!
   * R constructor for RCPP
   *
   */
  LatentOrderLikelihood(SEXP sexp){
    boost::shared_ptr<LatentOrderLikelihood> xp = unwrapRobject< LatentOrderLikelihood<Engine> >(sexp);
    model = xp->model;
    noTieModel = xp->noTieModel;
    order = xp->order;
  }
  
  /*!
   * coerce to R object. for RCPP
   */
  operator SEXP() const{
    return wrapInReferenceClass(*this,Engine::engineName() + "LatentOrderLikelihood");
  }
  
  virtual ShallowCopyable* vShallowCopyUnsafe() const{
    return new LatentOrderLikelihood(*this);
  }
  
  ~LatentOrderLikelihood(){}
  
  void setModel(const Model<Engine>& mod){
    model = mod.clone();
    noTieModel = mod.clone();
    noTieModel->setNetwork(mod.network()->clone());
    removeEdges(noTieModel);
    noTieModel->calculate();
  }
  
  
  void setThetas(std::vector<double> newThetas){
    model->setThetas(newThetas);
    noTieModel->setThetas(newThetas);
  }
  
  ModelPtr getModel(){
    return model;
  }
  
  
  /*!
   * Get model exposed to R
   */
  Rcpp::RObject getModelR(){
    return wrap(*model);
  }
  
  List variationalModelFrame(int nOrders, double downsampleRate){
    List result;
    
    long n = model->network()->size();
    for(int i=0; i<nOrders; i++){
      std::vector<int> vertices(n);
      if(model->hasVertexOrder()){
        this->generateOrder(vertices, model->getVertexOrder());
      }else{
        for(int i=0; i<n;i++){
          vertices[i] = i;
        }
        this->shuffle(vertices, n);
      }
      result.push_back(this->modelFrameGivenOrder(downsampleRate, vertices));
    }
    return result;
  }
  
  // Added in so can do variational model initialisation with truncated LOLOG
  // Keeps dyads that are present with probability 1 adds the right number of other empty dyads  
  List variationalModelFrameUnconstrained(int nOrders, double downsampleRate,double truncRate){
    List result;
    
    long n = model->network()->size();
    long e = n*(n-1);
    if(model->network()->isDirected()){
      e = e*0.5;
    }
    
    std::vector<int> perm_heads;
    std::vector<int> perm_tails;
    
    // get the edgelist and push them onto the heads and tails objects:
    boost::shared_ptr< std::vector< std::pair<int,int> > > edgelist = model->network()->edgelist();
    for(std::vector< std::pair<int,int>>::iterator i = edgelist->begin(); i < edgelist->end(); i++){
      perm_heads.push_back(i->first);
      perm_tails.push_back(i->second);
    }
    
    
    // Add randomly chosen edge, note that this allows for duplication of edges currently
    for(int i=0; i<nOrders; i++){
      std::vector<int> vertices(n);
      // Define the edge permutations sparsely shuffled
      std::vector<int> verts(n);
      std::iota(verts.begin(),verts.end(),0);

      for(int i=0; i < (e*truncRate - perm_heads.size()); i++){
        int ind_1 = floor(Rf_runif(0,1)*n);
        int ind_2 = floor(Rf_runif(0,1)*n);
        //Don't allow the same ind_1 and ind_2
        while(ind_1 == ind_2){
          ind_1 = floor(Rf_runif(0,1)*n);
          ind_2 = floor(Rf_runif(0,1)*n);
        }
        perm_heads.push_back(verts[ind_1]);
        perm_tails.push_back(verts[ind_2]);
      }

      // Shuffle the perm heads and tails so that the observed edges aren't always first:
      this->shuffle(perm_heads,perm_heads.size());
      this->shuffle(perm_tails,perm_tails.size());
      result.push_back(this->modelFrameGivenEdgeOrder(downsampleRate, perm_heads,perm_tails));
    }
    return result;
  }
  
  
  List variationalModelFrameWithFunc(int nOrders, double downsampleRate, Function vertexOrderingFunction){
    List result;
    for( int i=0; i<nOrders; i++){
      GetRNGstate();
      std::vector<int> vertices = as< std::vector<int> >(vertexOrderingFunction());
      PutRNGstate();
      result.push_back(this->modelFrameGivenOrder(downsampleRate, vertices));
    }
    return result;
  }
  
  List modelFrameGivenOrder(double downsampleRate, std::vector<int> vert_order){
    GetRNGstate();
    long n = model->network()->size();
    //long nStats = model->thetas().size();
    
    ModelPtr runningModel = noTieModel->clone();
    runningModel->setNetwork(noTieModel->network()->clone());
    runningModel->calculate();
    List samples;
    std::vector<double> terms = runningModel->statistics();
    std::vector<double>  newTerms = runningModel->statistics();
    
    std::vector<int> workingVertOrder = vert_order;
    
    std::vector<int> outcome;
    std::vector< std::vector<double> > predictors(terms.size());
    for(int i=0;i<predictors.size();i++){
      predictors.at(i).reserve(floor(downsampleRate * noTieModel->network()->maxEdges()) + 1000);
    }
    
    bool sample;
    //bool hasEdge;
    //double lpartition = 0.0;
    for(int i=0; i < n; i++){
      int vertex = workingVertOrder[i];
      this->shuffle(workingVertOrder,i);
      for(int j=0; j < i; j++){
        int alter = workingVertOrder[j];
        sample = Rf_runif(0.0,1.0) < downsampleRate;
        assert(!runningModel->network()->hasEdge(vertex, alter));
        bool hasEdge = model->network()->hasEdge(vertex, alter);
        if(sample){
          runningModel->statistics(terms);
          runningModel->dyadUpdate(vertex, alter, vert_order, i);
          runningModel->statistics(newTerms);
          
          if(hasEdge){
            runningModel->network()->toggle(vertex, alter);
          }else{
            runningModel->rollback();
          }
          outcome.push_back(hasEdge);
          for(int k=0; k<terms.size(); k++){
            predictors[k].push_back(newTerms[k] - terms[k]);
          }
        }else{
          if(hasEdge){
            runningModel->dyadUpdate(vertex, alter, vert_order, i);
            runningModel->network()->toggle(vertex, alter);
          }
        }
        
        if(runningModel->network()->isDirected()){
          hasEdge = model->network()->hasEdge(alter, vertex);
          if(sample){
            runningModel->statistics(terms);
            runningModel->dyadUpdate(alter, vertex, vert_order, i);
            runningModel->statistics(newTerms);
            
            if(hasEdge){
              runningModel->network()->toggle(alter, vertex);
            }else{
              runningModel->rollback();
            }
            outcome.push_back(hasEdge);
            for(int k=0; k<terms.size(); k++){
              predictors[k].push_back(newTerms[k] - terms[k]);
            }
          }else{
            if(hasEdge){
              runningModel->dyadUpdate(alter, vertex, vert_order, i);
              runningModel->network()->toggle(alter, vertex);
            }
          }
        }
        
        
      }
    }
    
    PutRNGstate();
    List result;
    result["outcome"] = wrap(outcome);
    result["samples"] = wrap(predictors);
    return result;
  }
  
  List modelFrameGivenEdgeOrder(double downsampleRate,
                                std::vector<int> perm_heads,
                                std::vector<int> perm_tails){
    GetRNGstate();
    long n = model->network()->size();
    //long nStats = model->thetas().size();
    
    ModelPtr runningModel = noTieModel->clone();
    runningModel->setNetwork(noTieModel->network()->clone());
    runningModel->calculate();
    List samples;
    std::vector<double> terms = runningModel->statistics();
    std::vector<double>  newTerms = runningModel->statistics();
    
    //Make vert order that isn't used
    std::vector<int> vertices(n);
    if(model->hasVertexOrder()){
      this->generateOrder(vertices, model->getVertexOrder());
    }else{
      for(int i=0; i<n;i++){
        vertices[i] = i;
      }
      this->shuffle(vertices, n);
    }
    std::vector<int> vert_order = vertices;
    
    std::vector<int> outcome;
    std::vector< std::vector<double> > predictors(terms.size());
    for(int i=0;i<predictors.size();i++){
      predictors.at(i).reserve(floor(downsampleRate * noTieModel->network()->maxEdges()) + 1000);
    }
    
    bool sample;
    //bool hasEdge;
    //double partition = 0.0;
    for(int i=0; i < perm_heads.size(); i++){
      int vertex = perm_tails[i];
      int alter  = perm_heads[i];
      sample = Rf_runif(0.0,1.0) < downsampleRate;
      assert(!runningModel->network()->hasEdge(vertex, alter));
      bool hasEdge = model->network()->hasEdge(vertex, alter);
      
      //initialise the actorIndex
      int actorIndex = 0;
      
      //Find which actor the vertex correponds to
      for(int k =0; k<n;k++){
        if(vert_order[k] == vertex){
          int actorIndex = k;
          break;
        }
      }
      
      if(sample){
        runningModel->statistics(terms);
        runningModel->dyadUpdate(vertex, alter, vert_order, actorIndex);
        runningModel->statistics(newTerms);
        
        if(hasEdge){
          runningModel->network()->toggle(vertex, alter);
        }else{
          runningModel->rollback();
        }
        outcome.push_back(hasEdge);
        for(int k=0; k<terms.size(); k++){
          predictors[k].push_back(newTerms[k] - terms[k]);
        }
      }else{
        if(hasEdge){
          runningModel->dyadUpdate(vertex, alter, vert_order, actorIndex);
          runningModel->network()->toggle(vertex, alter);
        }
      }
      
      if(runningModel->network()->isDirected()){
        hasEdge = model->network()->hasEdge(alter, vertex);
        if(sample){
          runningModel->statistics(terms);
          runningModel->dyadUpdate(alter, vertex, vert_order, actorIndex);
          runningModel->statistics(newTerms);
          
          if(hasEdge){
            runningModel->network()->toggle(alter, vertex);
          }else{
            runningModel->rollback();
          }
          outcome.push_back(hasEdge);
          for(int k=0; k<terms.size(); k++){
            predictors[k].push_back(newTerms[k] - terms[k]);
          }
        }else{
          if(hasEdge){
            runningModel->dyadUpdate(alter, vertex, vert_order, actorIndex);
            runningModel->network()->toggle(alter, vertex);
          }
        }
      }
    }
    
    PutRNGstate();
    List result;
    result["outcome"] = wrap(outcome);
    result["samples"] = wrap(predictors);
    return result;
  }
  
  Rcpp::RObject generateNetwork(){
    GetRNGstate();
    long n = model->network()->size();
    std::vector<int> vertices(n);
    if(model->hasVertexOrder()){
      this->generateOrder(vertices, model->getVertexOrder());
    }else{
      for(int i=0; i<n;i++){
        vertices[i] = i;
      }
      this->shuffle(vertices, n);
    }
    PutRNGstate();
    return this->generateNetworkWithOrder(vertices,false);
  }
  
  Rcpp::RObject generateNetworkReturnChanges(){
    GetRNGstate();
    long n = model->network()->size();
    std::vector<int> vertices(n);
    if(model->hasVertexOrder()){
      this->generateOrder(vertices, model->getVertexOrder());
    }else{
      for(int i=0; i<n;i++){
        vertices[i] = i;
      }
      this->shuffle(vertices, n);
    }
    PutRNGstate();
    return this->generateNetworkWithOrder(vertices,true);
  }
  
  // Method allows for edge orderings not to be constrained by being derived from sequentially adding nodes
  Rcpp::RObject generateNetworkUnconstrained(double truncRate=1){
    GetRNGstate();
    long n = model->network()->size();
    long e = n*(n-1);
    if(model->network()->isDirected()){
      e = e*0.5;
    }
    // Define the edge permutations sparsely shuffled
    std::vector<int> verts(n);
    std::iota(verts.begin(),verts.end(),0);
    std::vector<int> perm_head;
    std::vector<int> perm_tail;
    for(int i=0; i <e*truncRate; i++){
        int ind_1 = floor(Rf_runif(0,1)*n);
        int ind_2 = floor(Rf_runif(0,1)*n);
        //Don't allow the same ind_1 and ind_2
        while(ind_1 == ind_2){
          ind_1 = floor(Rf_runif(0,1)*n);
          ind_2 = floor(Rf_runif(0,1)*n);
        }
        perm_head.push_back(verts[ind_1]);
        perm_tail.push_back(verts[ind_2]);
    }
    
    // generate the network
    return this->generateNetworkWithEdgeOrder(perm_head,
                                              perm_tail);
  }
  
  Rcpp::RObject generateNetworkWithOrder(std::vector<int> vert_order,bool storeChangeStats=false){
    GetRNGstate();
    long n = model->network()->size();
    long nStats = model->thetas().size();
    
    //The model used for generating the network draw
    ModelPtr runningModel = noTieModel->clone();
    runningModel->setNetwork(noTieModel->network()->clone());
    runningModel->calculate();
    bool directedGraph = runningModel->network()->isDirected();
    
    Rcpp::List changeStats(1);
    if(storeChangeStats){
      //Make the change stat list
      long e = n*(n-1);
      if(!directedGraph){
        e = e*0.5;
      }
      Rcpp::List tmp(e);
      changeStats = tmp;
    }
    
    
    std::vector<double> eStats = std::vector<double>(nStats, 0.0);//runningModel->statistics();
    std::vector<double> stats = std::vector<double>(nStats, 0.0);
    std::vector<double> auxStats = std::vector<double>(nStats, 0.0);
    std::vector<double> terms = runningModel->statistics();
    std::vector<double>  newTerms = runningModel->statistics();
    std::vector<double>  emptyStats = runningModel->statistics();
    
    std::vector<int> workingVertOrder = vert_order;
    
    
    double llik = runningModel->logLik();
    double llikChange, probTie;//, ldenom;
    bool hasEdge = false;
    for(int i=0; i < n; i++){
      int vertex = workingVertOrder[i];
      this->shuffle(workingVertOrder,i);
      for(int j=0; j < i; j++){
        int alter = workingVertOrder[j];
        assert(!runningModel->network()->hasEdge(vertex, alter));
        llik = runningModel->logLik();
        runningModel->dyadUpdate(vertex, alter, vert_order, i);
        runningModel->statistics(newTerms);
        llikChange = runningModel->logLik() - llik;
        probTie = 1.0 / (1.0 + exp(-llikChange));
        hasEdge = false;
        if(Rf_runif(0.0, 1.0) < probTie){
          runningModel->network()->toggle(vertex, alter);
          hasEdge = true;
        }else
          runningModel->rollback();
        
        //update the generated network statistics and expected statistics
        //Initiate change stats
        std::vector<double> change(terms.size());
        
        for(int m=0; m<terms.size(); m++){
          double diff = newTerms[m] - terms[m];\
          eStats[m] += diff * probTie;
          change[m] = diff;
          if(hasEdge){
            stats[m] += diff;
            terms[m] += diff;
          }
        }
        if(storeChangeStats){
          if(directedGraph){
            changeStats[((i-1)*(i) + (2*j))] = change; //make sure we get the right one if directed
          }else{
            changeStats[((i-1)*(i)*0.5 + j)] = change;
          }
        }
        
        
        
        if(directedGraph){
          assert(!runningModel->network()->hasEdge(alter, vertex));
          llik = runningModel->logLik();
          runningModel->dyadUpdate(alter, vertex, vert_order, i);
          runningModel->statistics(newTerms);
          llikChange = runningModel->logLik() - llik;
          probTie = 1.0 / (1.0 + exp(-llikChange));
          hasEdge=false;
          if(Rf_runif(0.0, 1.0) < probTie){
            runningModel->network()->toggle(alter, vertex);
            hasEdge=true;
          }else
            runningModel->rollback();
          
          
          for(int m=0; m<terms.size(); m++){
            double diff = newTerms[m] - terms[m];
            eStats[m] += diff * probTie;
            change[m] = diff;
            if(hasEdge){
              stats[m] += diff;
              terms[m] += diff;
            }
          }
          if(storeChangeStats){
            changeStats[((i-1)*(i) + (2*j +1))] = change; //make sure we get the right one if directed
          }
        }
      }
    }
    std::vector<int> rankOrder = vert_order;
    for(int i=0;i<vert_order.size();i++)
      rankOrder[vert_order[i]] = i;
    DiscreteAttrib attr = DiscreteAttrib();
    attr.setName("__order__");
    runningModel->network()->addDiscreteVariable(rankOrder, attr);
    PutRNGstate();
    List result;
    result["network"] = runningModel->network()->cloneR();
    result["emptyNetworkStats"] = wrap(emptyStats);
    result["stats"] = wrap(stats);
    result["expectedStats"] = wrap(eStats);
    if(storeChangeStats){result["changeStats"] = wrap(changeStats);}
    
    return result;
  }
  
  //Based on generate model from vertex order - generate network based on edge ordering
  //Also returns the change stats used to generate the network
  Rcpp::RObject generateNetworkWithEdgeOrder(std::vector<int> perm_heads,
                                             std::vector<int> perm_tails){
    GetRNGstate();
    long n = model->network()->size();
    long nStats = model->thetas().size();
    long e = n*(n-1);
    if(!model->network()->isDirected()){
      e = e/2;
    }
    
    //throw warning if the perm_heads or perm_tails are the wrong length
    /*if(e != perm_heads.size() || e!= perm_tails.size()){
      Rcpp::Rcout<< "The perm head and tails are the wrong lengths, if doing full LOLOG something has gone badly wrong, if doing truncated LOLOG this is expected behaviour" ;
    }*/
    
    //Check if the max of perm head and perm tail are correct
    int max_tail = *std::max_element(perm_tails.begin(),perm_tails.end());
    int max_head = *std::max_element(perm_heads.begin(),perm_heads.end());
    if( max_tail > (n-1) || max_head > (n-1) ){
      Rcpp::Rcout<< "The perm has vertices that don't exist, probably forgot to minus 1 in the heads and tails from R to cpp" ;
    }
    
    //Make vert order that isn't used
    std::vector<int> vertices(n);
    if(model->hasVertexOrder()){
      this->generateOrder(vertices, model->getVertexOrder());
    }else{
      for(int i=0; i<n;i++){
        vertices[i] = i;
      }
      this->shuffle(vertices, n);
    }
    std::vector<int> vert_order = vertices;
    
    //The model used for generating the network draw
    ModelPtr runningModel = noTieModel->clone();
    runningModel->setNetwork(noTieModel->network()->clone());
    runningModel->calculate();
    
    //Make the change stat list
    Rcpp::List changeStats(e);
    
    
    std::vector<double> eStats = std::vector<double>(nStats, 0.0);//runningModel->statistics();
    std::vector<double> stats = std::vector<double>(nStats, 0.0);
    std::vector<double> auxStats = std::vector<double>(nStats, 0.0);
    std::vector<double> terms = runningModel->statistics();
    std::vector<double>  newTerms = runningModel->statistics();
    std::vector<double>  emptyStats = runningModel->statistics();
    int actorIndex = 1;
    
    //std::vector<int> workingVertOrder = vert_order;
    
    bool directedGraph = runningModel->network()->isDirected();
    double llik = runningModel->logLik();
    double llikChange, probTie;//, ldenom;
    bool hasEdge = false;
    for(int i=0; i < perm_tails.size(); i++){
      int vertex = perm_tails[i];
      int alter = perm_heads[i];
      
      //Don't need to assert this, to speed up
      //assert(!runningModel->network()->hasEdge(vertex, alter));
      
      llik = runningModel->logLik();
      
      //Find which actor the vertex correponds to
      for(int k =0; k<n;k++){
        if(vert_order[k] == vertex){
          int actorIndex = k;
          break;
        }
      }
      
      runningModel->dyadUpdate(vertex, alter, vert_order, actorIndex);
      runningModel->statistics(newTerms);
      llikChange = runningModel->logLik() - llik;
      probTie = 1.0 / (1.0 + exp(-llikChange));
      hasEdge = false;
      if(Rf_runif(0.0, 1.0) < probTie){
        runningModel->network()->toggle(vertex, alter);
        hasEdge = true;
      }else
        runningModel->rollback();
      
      //update the generated network statistics and expected statistics
      //Initiate change stats
      std::vector<double> change(terms.size());
      for(int m=0; m<terms.size(); m++){
        double diff = newTerms[m] - terms[m];\
        change[m] = diff;
        eStats[m] += diff * probTie;
        if(hasEdge){
          stats[m] += diff;
          terms[m] += diff;
        }
      }
      changeStats[i] = change;
    }
    std::vector<int> rankOrder = vert_order;
    for(int i=0;i<vert_order.size();i++)
      rankOrder[vert_order[i]] = i;
    DiscreteAttrib attr = DiscreteAttrib();
    attr.setName("__order__");
    runningModel->network()->addDiscreteVariable(rankOrder, attr);
    PutRNGstate();
    List result;
    result["network"] = runningModel->network()->cloneR();
    result["emptyNetworkStats"] = wrap(emptyStats);
    result["stats"] = wrap(stats);
    result["expectedStats"] = wrap(eStats);
    result["changeStats"] = wrap(changeStats);
    
    return result;
  }
  
  
  //Heavily modified from the generate network function
  Rcpp::List calcChangeStats(std::vector<int> perm_heads,
                             std::vector<int> perm_tails){
    
    long n = model->network()->size();
    long nStats = model->thetas().size();
    long e = n*(n-1);
    if(!model->network()->isDirected()){
      e = e/2;
    }
    
    //Make vert order that isn't used
    std::vector<int> vertices(n);
    if(model->hasVertexOrder()){
      this->generateOrder(vertices, model->getVertexOrder());
    }else{
      for(int i=0; i<n;i++){
        vertices[i] = i;
      }
      this->shuffle(vertices, n);
    }
    std::vector<int> vert_order = vertices;
    
    //Check if the perm_head and perm_tails vectors have the right number of elements
    if(perm_heads.size() != e || perm_tails.size() != e){
      Rcpp::Rcout<< "The perm is the wrong length" ;
    }
    
    //Check if the max of perm head and perm tail are correct
    int max_tail = *std::max_element(perm_tails.begin(),perm_tails.end());
    int max_head = *std::max_element(perm_heads.begin(),perm_heads.end());
    if( max_tail > (n-1) || max_head > (n-1) ){
      Rcpp::Rcout<< "The perm has vertices that don't exist, probably forgot to minus 1 in the heads and tails from R to cpp" ;
    }
    
    //The model used for calculating the change stats
    ModelPtr runningModel = noTieModel->clone();
    runningModel->setNetwork(noTieModel->network()->clone());
    runningModel->calculate();
    
    std::vector<double> eStats = std::vector<double>(nStats, 0.0);//runningModel->statistics();
    std::vector<double> stats = std::vector<double>(nStats, 0.0);
    std::vector<double> auxStats = std::vector<double>(nStats, 0.0);
    std::vector<double> terms = runningModel->statistics();
    std::vector<double>  newTerms = runningModel->statistics();
    std::vector<double>  emptyStats = runningModel->statistics();
    int actorIndex = 1;
    Rcpp::List result(e);
    
    
    //bool directedGraph = runningModel->network()->isDirected();
    // double llik = runningModel->logLik();
    // double llikChange, probTie;//, ldenom;
    for(int i=0; i < e; i++){
      int vertex = perm_tails[i];
      int alter = perm_heads[i];
      assert(!runningModel->network()->hasEdge(vertex, alter));
      //llik = runningModel->logLik();
      std::vector<double> stat = runningModel->statistics();
      
      //Find which actor the vertex correponds to
      for(int k =0; k<n;k++){
        if(vert_order[k] == vertex){
          actorIndex = k;
          break;
        }
      }
      
      runningModel->dyadUpdate(vertex, alter, vert_order, actorIndex);
      //runningModel->statistics(newTerms);
      std::vector<double> statNew(nStats);
      runningModel->statistics(statNew);
      std::vector<double> changeStat(nStats);
      std::transform(statNew.begin(), statNew.end(),stat.begin(),changeStat.begin(),std::minus<double>());
      result[i] = changeStat;
      if(model->network()->hasEdge(vertex,alter)){
        runningModel->network()->toggle(vertex,alter);
      }else{runningModel->rollback();}
    }
    return result;
  }
  
};


}
#endif /* LATENTORDERLIKELIHOOD_H_ */