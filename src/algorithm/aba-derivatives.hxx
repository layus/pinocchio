//
// Copyright (c) 2018-2020 CNRS INRIA
//

#ifndef __pinocchio_aba_derivatives_hxx__
#define __pinocchio_aba_derivatives_hxx__

#include "pinocchio/multibody/visitor.hpp"
#include "pinocchio/algorithm/check.hpp"
#include "pinocchio/algorithm/aba.hpp"

namespace pinocchio
{
  
  template<typename Scalar, int Options, template<typename,int> class JointCollectionTpl, typename ConfigVectorType, typename TangentVectorType>
  struct ComputeABADerivativesForwardStep1
  : public fusion::JointUnaryVisitorBase< ComputeABADerivativesForwardStep1<Scalar,Options,JointCollectionTpl,ConfigVectorType,TangentVectorType> >
  {
    typedef ModelTpl<Scalar,Options,JointCollectionTpl> Model;
    typedef DataTpl<Scalar,Options,JointCollectionTpl> Data;
    
    typedef boost::fusion::vector<const Model &,
                                  Data &,
                                  const ConfigVectorType &,
                                  const TangentVectorType &
                                  > ArgsType;
    
    template<typename JointModel>
    static void algo(const JointModelBase<JointModel> & jmodel,
                     JointDataBase<typename JointModel::JointDataDerived> & jdata,
                     const Model & model,
                     Data & data,
                     const Eigen::MatrixBase<ConfigVectorType> & q,
                     const Eigen::MatrixBase<TangentVectorType> & v)
    {
      typedef typename Model::JointIndex JointIndex;
      
      const JointIndex & i = jmodel.id();
      const JointIndex & parent = model.parents[i];
      typename Data::Motion & ov = data.ov[i];

      jmodel.calc(jdata.derived(),q.derived(),v.derived());
      
      data.liMi[i] = model.jointPlacements[i]*jdata.M();
      if(parent > 0)
        data.oMi[i] = data.oMi[parent] * data.liMi[i];
      else
        data.oMi[i] = data.liMi[i];
      
      ov = data.oMi[i].act(jdata.v());
      if(parent > 0)
        ov += data.ov[parent];

      data.oa_gf[i] = data.oMi[i].act(jdata.c());
      if(parent > 0)
        data.oa_gf[i] += (data.ov[parent] ^ ov);

      data.oYcrb[i] = data.oMi[i].act(model.inertias[i]);
      data.oYaba[i] = data.oYcrb[i].matrix();

      data.oh[i] = data.oYcrb[i] * ov;
      data.of[i] = ov.cross(data.oh[i]);

      typedef typename SizeDepType<JointModel::NV>::template ColsReturn<typename Data::Matrix6x>::Type ColsBlock;
      ColsBlock J_cols = jmodel.jointCols(data.J);
      J_cols = data.oMi[i].act(jdata.S());
    }
    
  };
  
  template<typename Scalar, int Options, template<typename,int> class JointCollectionTpl, typename MatrixType>
  struct ComputeABADerivativesBackwardStep1
  : public fusion::JointUnaryVisitorBase< ComputeABADerivativesBackwardStep1<Scalar,Options,JointCollectionTpl,MatrixType> >
  {
    typedef ModelTpl<Scalar,Options,JointCollectionTpl> Model;
    typedef DataTpl<Scalar,Options,JointCollectionTpl> Data;
    
    typedef boost::fusion::vector<const Model &,
                                  Data &,
                                  MatrixType &>  ArgsType;
    
    template<typename JointModel>
    static void algo(const JointModelBase<JointModel> & jmodel,
                     JointDataBase<typename JointModel::JointDataDerived> & jdata,
                     const Model & model,
                     Data & data,
                     const Eigen::MatrixBase<MatrixType> & Minv)
    {
      typedef typename Model::JointIndex JointIndex;
      typedef typename Data::Inertia Inertia;
      typedef typename Data::Force Force;
      typedef typename Data::Matrix6x Matrix6x;
      
      const JointIndex & i = jmodel.id();
      const JointIndex & parent = model.parents[i];

      typename Inertia::Matrix6 & Ia = data.oYaba[i];
      
      Matrix6x & Fcrb = data.Fcrb[0];
      Matrix6x & FcrbTmp = data.Fcrb.back();

      typedef typename SizeDepType<JointModel::NV>::template ColsReturn<typename Data::Matrix6x>::Type ColsBlock;
      Force & fi = data.of[i];
      
      typedef typename SizeDepType<JointModel::NV>::template ColsReturn<Matrix6x>::Type ColBlock;
      const ColBlock J_cols = jmodel.jointCols(data.J);
      
      jmodel.jointVelocitySelector(data.u).noalias() -= J_cols.transpose()*fi.toVector();
      
      jdata.U().noalias() = Ia * J_cols;
      jdata.StU().noalias() = J_cols.transpose() * jdata.U();
      
      internal::PerformStYSInversion<Scalar>::run(jdata.StU(),jdata.Dinv());
      jdata.UDinv().noalias() = jdata.U() * jdata.Dinv();
      
      MatrixType & Minv_ = PINOCCHIO_EIGEN_CONST_CAST(MatrixType,Minv);
      
      Minv_.block(jmodel.idx_v(),jmodel.idx_v(),jmodel.nv(),jmodel.nv()) = jdata.Dinv();
      const int nv_children = data.nvSubtree[i] - jmodel.nv();
      if(nv_children > 0)
      {
        ColsBlock SDinv_cols = jmodel.jointCols(data.SDinv);
        SDinv_cols.noalias() = J_cols * jdata.Dinv();
        
        Minv_.block(jmodel.idx_v(),jmodel.idx_v()+jmodel.nv(),jmodel.nv(),nv_children).noalias()
        = -SDinv_cols.transpose() * Fcrb.middleCols(jmodel.idx_v()+jmodel.nv(),nv_children);
        
        if(parent > 0)
        {
          FcrbTmp.leftCols(data.nvSubtree[i]).noalias()
          = jdata.U() * Minv_.block(jmodel.idx_v(),jmodel.idx_v(),jmodel.nv(),data.nvSubtree[i]);
          Fcrb.middleCols(jmodel.idx_v(),data.nvSubtree[i]) += FcrbTmp.leftCols(data.nvSubtree[i]);
        }
      }
      else // This a leaf of the kinematic tree
      {
        Fcrb.middleCols(jmodel.idx_v(),data.nvSubtree[i]).noalias()
        = jdata.U() * Minv_.block(jmodel.idx_v(),jmodel.idx_v(),jmodel.nv(),data.nvSubtree[i]);
      }

      if (parent > 0)
      {
        Ia.noalias() -= jdata.UDinv() * jdata.U().transpose();
        
        fi.toVector().noalias() += Ia * data.oa_gf[i].toVector() + jdata.UDinv() * jmodel.jointVelocitySelector(data.u);
        data.oYaba[parent] += Ia;
        data.of[parent] += fi;
      }

    }
    
  };
  
  template<typename Scalar, int Options, template<typename,int> class JointCollectionTpl, typename MatrixType>
  struct ComputeABADerivativesForwardStep2
  : public fusion::JointUnaryVisitorBase< ComputeABADerivativesForwardStep2<Scalar,Options,JointCollectionTpl,MatrixType> >
  {
    typedef ModelTpl<Scalar,Options,JointCollectionTpl> Model;
    typedef DataTpl<Scalar,Options,JointCollectionTpl> Data;
    
    typedef boost::fusion::vector<const Model &,
                                  Data &,
                                  MatrixType &> ArgsType;
    
    template<typename JointModel>
    static void algo(const JointModelBase<JointModel> & jmodel,
                     JointDataBase<typename JointModel::JointDataDerived> & jdata,
                     const Model & model,
                     Data & data,
                     MatrixType & Minv)
    {
      typedef typename Model::JointIndex JointIndex;
      typedef typename Data::Matrix6x Matrix6x;
      
      const JointIndex & i = jmodel.id();
      const JointIndex & parent = model.parents[i];
      
      typename Data::Motion & ov = data.ov[i];
      typename Data::Motion & oa = data.oa[i];
      typename Data::Force & of = data.of[i];
      typename Data::Motion & oa_gf = data.oa_gf[i];
      
      typedef typename SizeDepType<JointModel::NV>::template ColsReturn<Matrix6x>::Type ColsBlock;
      ColsBlock J_cols = jmodel.jointCols(data.J);
      
      oa_gf += data.oa_gf[parent];
      jmodel.jointVelocitySelector(data.ddq).noalias() =
      jdata.Dinv() * jmodel.jointVelocitySelector(data.u) - jdata.UDinv().transpose() * oa_gf.toVector();
      
      oa_gf.toVector().noalias() += J_cols * jmodel.jointVelocitySelector(data.ddq);
      oa = oa_gf + model.gravity;
      of = data.oYcrb[i] * oa_gf + ov.cross(data.oh[i]);

      typename Data::Matrix6x & FcrbTmp = data.Fcrb.back();
      MatrixType & Minv_ = PINOCCHIO_EIGEN_CONST_CAST(MatrixType,Minv);
      
      if(parent > 0)
      {
        FcrbTmp.topRows(jmodel.nv()).rightCols(model.nv - jmodel.idx_v()).noalias()
        = jdata.UDinv().transpose() * data.Fcrb[parent].rightCols(model.nv - jmodel.idx_v());
        Minv_.middleRows(jmodel.idx_v(),jmodel.nv()).rightCols(model.nv - jmodel.idx_v())
        -= FcrbTmp.topRows(jmodel.nv()).rightCols(model.nv - jmodel.idx_v());
      }
      
      data.Fcrb[i].rightCols(model.nv - jmodel.idx_v()).noalias()
      = J_cols * Minv_.middleRows(jmodel.idx_v(),jmodel.nv()).rightCols(model.nv - jmodel.idx_v());
      if(parent > 0)
        data.Fcrb[i].rightCols(model.nv - jmodel.idx_v()) += data.Fcrb[parent].rightCols(model.nv - jmodel.idx_v());
      
      ColsBlock dJ_cols = jmodel.jointCols(data.dJ);
      ColsBlock dVdq_cols = jmodel.jointCols(data.dVdq);
      ColsBlock dAdq_cols = jmodel.jointCols(data.dAdq);
      ColsBlock dAdv_cols = jmodel.jointCols(data.dAdv);
      
      motionSet::motionAction(ov,J_cols,dJ_cols);
      motionSet::motionAction(data.oa_gf[parent],J_cols,dAdq_cols);
      dAdv_cols = dJ_cols;
      if(parent > 0)
      {
        motionSet::motionAction(data.ov[parent],J_cols,dVdq_cols);
        motionSet::motionAction<ADDTO>(data.ov[parent],dVdq_cols,dAdq_cols);
        dAdv_cols += dVdq_cols;
      }
      else
        dVdq_cols.setZero();
      
      // computes variation of inertias
      data.doYcrb[i] = data.oYcrb[i].variation(ov);
      addForceCrossMatrix(data.oh[i],data.doYcrb[i]);
    }
    
    template<typename ForceDerived, typename M6>
    static void addForceCrossMatrix(const ForceDense<ForceDerived> & f,
                                    const Eigen::MatrixBase<M6> & mout)
    {
      M6 & mout_ = PINOCCHIO_EIGEN_CONST_CAST(M6,mout);
      addSkew(-f.linear(),mout_.template block<3,3>(ForceDerived::LINEAR,ForceDerived::ANGULAR));
      addSkew(-f.linear(),mout_.template block<3,3>(ForceDerived::ANGULAR,ForceDerived::LINEAR));
      addSkew(-f.angular(),mout_.template block<3,3>(ForceDerived::ANGULAR,ForceDerived::ANGULAR));
    }
    
  };
  
  template<typename Scalar, int Options, template<typename,int> class JointCollectionTpl>
  struct ComputeABADerivativesBackwardStep2
  : public fusion::JointUnaryVisitorBase< ComputeABADerivativesBackwardStep2<Scalar,Options,JointCollectionTpl> >
  {
    typedef ModelTpl<Scalar,Options,JointCollectionTpl> Model;
    typedef DataTpl<Scalar,Options,JointCollectionTpl> Data;
    
    typedef boost::fusion::vector<const Model &,
                                  Data &>  ArgsType;
    
    template<typename JointModel>
    static void algo(const JointModelBase<JointModel> & jmodel,
                     const Model & model,
                     Data & data)
    {
      typedef typename Model::JointIndex JointIndex;
      typedef Eigen::Matrix<Scalar,JointModel::NV,6,Options,JointModel::NV==Eigen::Dynamic?6:JointModel::NV,6> MatrixNV6;
      
      const JointIndex & i = jmodel.id();
      const JointIndex & parent = model.parents[i];
      
      typename PINOCCHIO_EIGEN_PLAIN_ROW_MAJOR_TYPE(MatrixNV6) Mat_tmp(jmodel.nv(),6);
      
      typename Data::MatrixXs & rnea_partial_dq = data.dtau_dq;
      typename Data::MatrixXs & rnea_partial_dv = data.dtau_dv;

      typedef typename SizeDepType<JointModel::NV>::template ColsReturn<typename Data::Matrix6x>::Type ColsBlock;
      
      ColsBlock J_cols = jmodel.jointCols(data.J);
      ColsBlock dVdq_cols = jmodel.jointCols(data.dVdq);
      ColsBlock dAdq_cols = jmodel.jointCols(data.dAdq);
      ColsBlock dAdv_cols = jmodel.jointCols(data.dAdv);
      ColsBlock dFdq_cols = jmodel.jointCols(data.dFdq);
      ColsBlock dFdv_cols = jmodel.jointCols(data.dFdv);

      // dtau/dv
      motionSet::inertiaAction(data.oYcrb[i],dAdv_cols,dFdv_cols);
      dFdv_cols.noalias() += data.doYcrb[i] * J_cols;
      
      rnea_partial_dv.block(jmodel.idx_v(),jmodel.idx_v(),jmodel.nv(),data.nvSubtree[i]).noalias()
      = J_cols.transpose()*data.dFdv.middleCols(jmodel.idx_v(),data.nvSubtree[i]);
      
      // dtau/dq
      motionSet::inertiaAction(data.oYcrb[i],dAdq_cols,dFdq_cols);
      if(parent>0)
        dFdq_cols += data.doYcrb[i] * dVdq_cols;
      
      rnea_partial_dq.block(jmodel.idx_v(),jmodel.idx_v(),jmodel.nv(),data.nvSubtree[i]).noalias()
      = J_cols.transpose()*data.dFdq.middleCols(jmodel.idx_v(),data.nvSubtree[i]);
      
      motionSet::act<ADDTO>(J_cols,data.of[i],dFdq_cols);
      
      if(parent > 0)
      {
        lhsInertiaMult(data.oYcrb[i],J_cols.transpose(),Mat_tmp);
        for(int j = data.parents_fromRow[(JointIndex)jmodel.idx_v()];j >= 0; j = data.parents_fromRow[(JointIndex)j])
          rnea_partial_dq.middleRows(jmodel.idx_v(),jmodel.nv()).col(j).noalias() = Mat_tmp * data.dAdq.col(j);
        for(int j = data.parents_fromRow[(JointIndex)jmodel.idx_v()];j >= 0; j = data.parents_fromRow[(JointIndex)j])
          rnea_partial_dv.middleRows(jmodel.idx_v(),jmodel.nv()).col(j).noalias() = Mat_tmp * data.dAdv.col(j);
        
        Mat_tmp.noalias() = J_cols.transpose() * data.doYcrb[i];
        for(int j = data.parents_fromRow[(JointIndex)jmodel.idx_v()];j >= 0; j = data.parents_fromRow[(JointIndex)j])
          rnea_partial_dq.middleRows(jmodel.idx_v(),jmodel.nv()).col(j) += Mat_tmp * data.dVdq.col(j);
        for(int j = data.parents_fromRow[(JointIndex)jmodel.idx_v()];j >= 0; j = data.parents_fromRow[(JointIndex)j])
          rnea_partial_dv.middleRows(jmodel.idx_v(),jmodel.nv()).col(j) += Mat_tmp * data.J.col(j);
      }
      
      if(parent>0)
      {
        data.oYcrb[parent] += data.oYcrb[i];
        data.doYcrb[parent] += data.doYcrb[i];
        data.of[parent] += data.of[i];
      }
      
      // Restore the status of dAdq_cols (remove gravity)
      assert(model.gravity.angular().isZero() && "The gravity must be a pure force vector, no angular part");
      for(Eigen::DenseIndex k =0; k < jmodel.nv(); ++k)
      {
        MotionRef<typename ColsBlock::ColXpr> min(J_cols.col(k));
        MotionRef<typename ColsBlock::ColXpr> mout(dAdq_cols.col(k));
        mout.linear() += model.gravity.linear().cross(min.angular());
      }
    }
    
    template<typename Min, typename Mout>
    static void lhsInertiaMult(const typename Data::Inertia & Y,
                               const Eigen::MatrixBase<Min> & J,
                               const Eigen::MatrixBase<Mout> & F)
    {
      Mout & F_ = PINOCCHIO_EIGEN_CONST_CAST(Mout,F);
      motionSet::inertiaAction(Y,J.derived().transpose(),F_.transpose());
    }
  };
  
  template<typename Scalar, int Options, template<typename,int> class JointCollectionTpl, typename ConfigVectorType, typename TangentVectorType1, typename TangentVectorType2,
  typename MatrixType1, typename MatrixType2, typename MatrixType3>
  inline void computeABADerivatives(const ModelTpl<Scalar,Options,JointCollectionTpl> & model,
                                    DataTpl<Scalar,Options,JointCollectionTpl> & data,
                                    const Eigen::MatrixBase<ConfigVectorType> & q,
                                    const Eigen::MatrixBase<TangentVectorType1> & v,
                                    const Eigen::MatrixBase<TangentVectorType2> & tau,
                                    const Eigen::MatrixBase<MatrixType1> & aba_partial_dq,
                                    const Eigen::MatrixBase<MatrixType2> & aba_partial_dv,
                                    const Eigen::MatrixBase<MatrixType3> & aba_partial_dtau)
  {
    PINOCCHIO_CHECK_INPUT_ARGUMENT(q.size() == model.nq, "The joint configuration vector is not of right size");
    PINOCCHIO_CHECK_INPUT_ARGUMENT(v.size() == model.nv, "The joint velocity vector is not of right size");
    PINOCCHIO_CHECK_INPUT_ARGUMENT(tau.size() == model.nv, "The joint acceleration vector is not of right size");
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dq.cols() == model.nv);
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dq.rows() == model.nv);
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dv.cols() == model.nv);
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dv.rows() == model.nv);
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dtau.cols() == model.nv);
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dtau.rows() == model.nv);
    assert(model.check(data) && "data is not consistent with model.");
    
    typedef typename ModelTpl<Scalar,Options,JointCollectionTpl>::JointIndex JointIndex;
    
    data.oa_gf[0] = -model.gravity;
    data.u = tau;
    
    MatrixType3 & Minv_ = PINOCCHIO_EIGEN_CONST_CAST(MatrixType3,aba_partial_dtau);
    Minv_.template triangularView<Eigen::Upper>().setZero();
    
    /// First, compute Minv and a, the joint acceleration vector
    typedef ComputeABADerivativesForwardStep1<Scalar,Options,JointCollectionTpl,ConfigVectorType,TangentVectorType1> Pass1;
    for(JointIndex i=1; i<(JointIndex) model.njoints; ++i)
    {
      Pass1::run(model.joints[i],data.joints[i],
                 typename Pass1::ArgsType(model,data,q.derived(),v.derived()));
    }
    
    data.Fcrb[0].setZero();
    typedef ComputeABADerivativesBackwardStep1<Scalar,Options,JointCollectionTpl,MatrixType3> Pass2;
    for(JointIndex i=(JointIndex)(model.njoints-1);i>0;--i)
    {
      Pass2::run(model.joints[i],data.joints[i],
                 typename Pass2::ArgsType(model,data,Minv_));
    }
    
    typedef ComputeABADerivativesForwardStep2<Scalar,Options,JointCollectionTpl,MatrixType3> Pass3;
    for(JointIndex i=1; i<(JointIndex) model.njoints; ++i)
    {
      Pass3::run(model.joints[i],data.joints[i],
                 typename Pass3::ArgsType(model,data,Minv_));
    }
    
    typedef ComputeABADerivativesBackwardStep2<Scalar,Options,JointCollectionTpl> Pass4;
    for(JointIndex i=(JointIndex)(model.njoints-1);i>0;--i)
    {
      Pass4::run(model.joints[i],
                 typename Pass4::ArgsType(model,data));
    }
    
    Minv_.template triangularView<Eigen::StrictlyLower>()
    = Minv_.transpose().template triangularView<Eigen::StrictlyLower>();
    
    PINOCCHIO_EIGEN_CONST_CAST(MatrixType1,aba_partial_dq).noalias() = -Minv_*data.dtau_dq;
    PINOCCHIO_EIGEN_CONST_CAST(MatrixType2,aba_partial_dv).noalias() = -Minv_*data.dtau_dv;
  }
  
  template<typename Scalar, int Options, template<typename,int> class JointCollectionTpl, typename ConfigVectorType, typename TangentVectorType1, typename TangentVectorType2,
  typename MatrixType1, typename MatrixType2, typename MatrixType3>
  inline void computeABADerivatives(const ModelTpl<Scalar,Options,JointCollectionTpl> & model,
                                    DataTpl<Scalar,Options,JointCollectionTpl> & data,
                                    const Eigen::MatrixBase<ConfigVectorType> & q,
                                    const Eigen::MatrixBase<TangentVectorType1> & v,
                                    const Eigen::MatrixBase<TangentVectorType2> & tau,
                                    const container::aligned_vector< ForceTpl<Scalar,Options> > & fext,
                                    const Eigen::MatrixBase<MatrixType1> & aba_partial_dq,
                                    const Eigen::MatrixBase<MatrixType2> & aba_partial_dv,
                                    const Eigen::MatrixBase<MatrixType3> & aba_partial_dtau)
  {
    PINOCCHIO_CHECK_INPUT_ARGUMENT(q.size() == model.nq, "The joint configuration vector is not of right size");
    PINOCCHIO_CHECK_INPUT_ARGUMENT(v.size() == model.nv, "The joint velocity vector is not of right size");
    PINOCCHIO_CHECK_INPUT_ARGUMENT(tau.size() == model.nv, "The joint acceleration vector is not of right size");
    PINOCCHIO_CHECK_INPUT_ARGUMENT(fext.size() == (size_t)model.njoints, "The size of the external forces is not of right size");
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dq.cols() == model.nv);
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dq.rows() == model.nv);
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dv.cols() == model.nv);
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dv.rows() == model.nv);
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dtau.cols() == model.nv);
    PINOCCHIO_CHECK_INPUT_ARGUMENT(aba_partial_dtau.rows() == model.nv);
    assert(model.check(data) && "data is not consistent with model.");
    
    typedef typename ModelTpl<Scalar,Options,JointCollectionTpl>::JointIndex JointIndex;
    
    data.oa_gf[0] = -model.gravity;
    data.u = tau;
    
    MatrixType3 & Minv_ = PINOCCHIO_EIGEN_CONST_CAST(MatrixType3,aba_partial_dtau);
    Minv_.template triangularView<Eigen::Upper>().setZero();
    
    /// First, compute Minv and a, the joint acceleration vector
    typedef ComputeABADerivativesForwardStep1<Scalar,Options,JointCollectionTpl,ConfigVectorType,TangentVectorType1> Pass1;
    for(JointIndex i=1; i<(JointIndex) model.njoints; ++i)
    {
      Pass1::run(model.joints[i],data.joints[i],
                 typename Pass1::ArgsType(model,data,q.derived(),v.derived()));
      data.of[i] -= data.oMi[i].act(fext[i]);
    }
    
    data.Fcrb[0].setZero();
    typedef ComputeABADerivativesBackwardStep1<Scalar,Options,JointCollectionTpl,MatrixType3> Pass2;
    for(JointIndex i=(JointIndex)(model.njoints-1);i>0;--i)
    {
      Pass2::run(model.joints[i],data.joints[i],
                 typename Pass2::ArgsType(model,data,Minv_));
    }
    
    typedef ComputeABADerivativesForwardStep2<Scalar,Options,JointCollectionTpl,MatrixType3> Pass3;
    for(JointIndex i=1; i<(JointIndex) model.njoints; ++i)
    {
      Pass3::run(model.joints[i],data.joints[i],
                 typename Pass3::ArgsType(model,data,Minv_));
      data.of[i] -= data.oMi[i].act(fext[i]);
    }
    
    typedef ComputeABADerivativesBackwardStep2<Scalar,Options,JointCollectionTpl> Pass4;
    for(JointIndex i=(JointIndex)(model.njoints-1);i>0;--i)
    {
      Pass4::run(model.joints[i],
                 typename Pass4::ArgsType(model,data));
    }
    
    Minv_.template triangularView<Eigen::StrictlyLower>()
    = Minv_.transpose().template triangularView<Eigen::StrictlyLower>();
    
    PINOCCHIO_EIGEN_CONST_CAST(MatrixType1,aba_partial_dq).noalias() = -Minv_*data.dtau_dq;
    PINOCCHIO_EIGEN_CONST_CAST(MatrixType2,aba_partial_dv).noalias() = -Minv_*data.dtau_dv;
  }
  
  
} // namespace pinocchio

#endif // ifndef __pinocchio_aba_derivatives_hxx__

