#ifndef RIGID_BODY_TREE_H_
#define RIGID_BODY_TREE_H_

#include "simbody/internal/common.h"
#include "simbody/internal/SimbodySubsystem.h"
#include "SimbodyTreeState.h"
using namespace SimTK;

class RigidBodyNode;
class ConstraintNode;
class RBDistanceConstraint;
class RBStation;

namespace SimTK {
class SBModelingVars;
class SBParameterVars;
class SBTimeVars;
class SBConfigurationVars;
class SBMotionVars;
class SBDynamicsVars;
class SBReactionVars;
class SBModelingCache;
class SBParameterCache;
class SBTimeCache;
class SBConfigurationCache;
class SBMotionCache;
class SBDynamicsCache;
class SBReactionCache;
}

#include <cassert>
#include <vector>
#include <iostream>

typedef std::vector<RigidBodyNode*>   RBNodePtrList;
typedef Vector_<SpatialVec>           SpatialVecList;

class IVM;
class LengthConstraints;

/**
 * The RigidBodyTree class owns the tree of joint-connected rigid bodies, called
 * RigidBodyNodes. The tree is stored by levels, with level 0 being ground, level 1
 * being bodies which are connected to ground (base bodies), level 2 connected to
 * level 1 and so on. Nodes at the same level are stored together in an array,
 * but the order does not reflect the logical tree structure; that is maintained
 * via parent & children pointers kept in the nodes.
 * 
 * RigidBodyTree is the owner of the RigidBodyNode objects (which are abstract), pointers to
 * which are stored in the tree.
 */
class RigidBodyTree {
public:
    RigidBodyTree() 
      : nextUSlot(0), nextUSqSlot(0), nextQSlot(0), DOFTotal(-1), SqDOFTotal(-1), maxNQTotal(-1), 
        built(false), constructionCacheIndex(-1), lConstraints(0) 
      { addGroundNode(); }

    RigidBodyTree(const RigidBodyTree&);
    RigidBodyTree& operator=(const RigidBodyTree&);
    ~RigidBodyTree();

    // Create a new node, add it to the tree, and assign it
    // a node number, which is a regular labeling starting with node 0 which is ground.
    int addRigidBodyNode
        (RigidBodyNode&          parent,
         const MassProperties&   m,            // mass properties in body frame
         const Transform&        X_PJb,        // parent's frame for attaching this joint
         const Transform&        X_BJ,         // inboard joint frame J in body frame
         JointSpecification::JointType        
                                 type,
         bool                    isReversed,   // child-to-parent orientation?
         int&                    nxtU,
         int&                    nxtUSq,
         int&                    nxtQ); 


    // Constrain stations on each of two distinct bodies to remain a
    // particular distance apart at all times. Distance must be
    // significantly greater than 0 so that this can be implemented as a
    // single constraint force acting along the instantaneous line between
    // the stations. Parent and child distinction here is meaningless.
    int addConstantDistanceConstraint(const RigidBodyNode& parent, const Vec3& stationInP,
                                      const RigidBodyNode& child,  const Vec3& stationInC,
                                      const Real& distance);

    // Constrain stations on each of two distinct bodies to remain superimposed.
    // This restricts all translation but no rotation so adds three constraint
    // equations. Parent and child distinction here is meaningless.
    int addCoincidentStationsConstraint(const RigidBodyNode& parent, const Vec3& stationInP,
                                        const RigidBodyNode& child,  const Vec3& stationInC);

    // Constrain frames fixed to each of two distinct bodies to remain
    // superimposed. Parent and child here mean nothing! This adds six
    // constraint equations.
    int addWeldConstraint(const RigidBodyNode& parent, const Transform& frameInP,
                          const RigidBodyNode& child,  const Transform& frameInC);

    // Call this after all bodies & constraints have been added.
    void endConstruction(); // will set built==true

    void realizeConstruction (State&) const;
    void realizeModeling     (State&) const;
    void realizeParameters   (const State&) const;
    void realizeTime         (const State&) const;
    void realizeConfiguration(const State&) const;
    void realizeMotion       (const State&) const;
    void realizeDynamics     (const State&) const;
    void realizeReaction     (const State&) const;

    Real calcKineticEnergy(const State&) const;


    /// Calculate the product J*X where J is the partial velocity Jacobian dV/du
    /// and X is a vector of SpatialVec's, one per body. See Eq. 76&77 in
    /// Schwieters' paper, and see 81a & b for a use of this routine to compute
    /// energy gradient in internal coordinates. In that case X=dE/dR, that is
    /// the gradient of the energy w.r.t. atomic positions, summed and shifted
    /// to body origins. There we are pretending dR/dq is the same as dV/du, which
    /// will be true if dq/dt = u, which works for all cases except quaternions.
    /// Schwieters handles that by using Euler angles for orientation coordinates
    /// when doing minimizations. But note that the routine works in terms of u,
    /// not q, so it produces a meaningful result in all cases, just not one that
    /// can be mapped directly back to quaternion coordinates. This is an O(n)
    /// operator which can be called after realizeConfiguration().
    /// It has no effect on the cache.
    void calcInternalGradientFromSpatial(const State&, 
        const SpatialVecList& X, 
        Vector&               JX);

    // Given a set of body forces, return the equivalent set of joint torques 
    // IGNORING CONSTRAINTS.
    // Must be in DynamicsStage so that articulated body inertias are available,
    // however, velocities are ignored. This operator has NO effect on the state
    // cache. It makes a single O(N) pass.
    void calcTreeEquivalentJointForces(const State&, 
        const Vector_<SpatialVec>& bodyForces,
        Vector&                    jointForces);

    void calcTreeAccelerations(const State& s,
        const Vector&              jointForces,
        const Vector_<SpatialVec>& bodyForces,
        Vector&                    netHingeForces,
        Vector_<SpatialVec>&       A_GB,
        Vector&                    udot) const; 

    // Must be in Stage::Configured to calculate qdot = Q*u.
    void calcQDot(const State& s,
        const Vector& u,
        Vector&       qdot) const;

    // Must be in MovingStage to calculate qdotdot = Qdot*u + Q*udot.
    void calcQDotDot(const State& s,
        const Vector& udot,
        Vector&       qdotdot) const;

    void setDefaultModelingValues     (const SBConstructionCache&, SBModelingVars&)      const;
    void setDefaultParameterValues    (const SBModelingVars&, SBParameterVars&)     const;
    void setDefaultTimeValues         (const SBModelingVars&, SBTimeVars&)          const;
    void setDefaultConfigurationValues(const SBModelingVars&, Vector& q)            const;
    void setDefaultMotionValues       (const SBModelingVars&, Vector& u)            const;
    void setDefaultDynamicsValues     (const SBModelingVars&, SBDynamicsVars&)      const;
    void setDefaultReactionValues     (const SBModelingVars&, SBReactionVars&)      const;

    // These counts can be obtained even during construction, where they
    // just return the current counts.
    // includes ground
    int getNBodies()      const {return nodeNum2NodeMap.size();}
    int getNConstraints() const {return constraintNodes.size();}

    // A single constraint may generate multiple of these.
    int getNDistanceConstraints() const {return distanceConstraints.size();}

        // CALLABLE AFTER realizeConstruction()

    int getTotalDOF()    const {assert(built); return DOFTotal;}
    int getTotalSqDOF()  const {assert(built); return SqDOFTotal;}
    int getTotalQAlloc() const {assert(built); return maxNQTotal;}

    int getTotalMultAlloc() const {assert(built); assert(false); return -1;} // TODO

    int getQIndex(int body) const;
    int getQAlloc(int body) const;
    int getUIndex(int body) const;
    int getDOF   (int body) const;

    int getMultIndex(int constraint) const {assert(built);assert(false); return -1;} //TODO
    int getMaxNMult (int constraint) const {assert(built);assert(false); return -1;} //TODO


    // Modeling info.

    void setUseEulerAngles(State& s, bool useAngles) const;
    void setJointIsPrescribed(State& s, int joint, bool prescribe) const;
    void setConstraintIsEnabled(State& s, int constraint, bool enable) const;
    bool getUseEulerAngles(const State& s) const;
    bool isJointPrescribed(const State& s, int joint) const;
    bool isConstraintEnabled(const State& s, int constraint) const;

        // CALLABLE AFTER realizeModeling()

    const Real& getJointQ(const State& s, int body, int axis) const;
    const Real& getJointU(const State& s, int body, int axis) const;

    void setJointQ(State& s, int body, int axis, const Real& r) const;
    void setJointU(State& s, int body, int axis, const Real& r) const;
    void setPrescribedUdot(State& s, int body, int axis, const Real& r) const;


    const Vector& getAppliedJointForces(const State&) const;
    const Vector_<SpatialVec>& getAppliedBodyForces(const State&) const;

    // Call after realizeConfiguration()
    const Transform& getBodyConfiguration(const State& s, int body) const;

    // Call after realizeMotion()
    const SpatialVec& getBodyVelocity(const State& s, int body) const;


    // Call after realizeReactions()
    const SpatialVec& getBodyAcceleration(const State& s, int body) const;



    // Dynamics -- calculate accelerations and internal forces from 
    // forces and prescribed accelerations supplied in the State.


    void clearAppliedForces(State& s) const;
    void applyGravity(State& s, const Vec3& g) const;
    void applyPointForce(State& s, int body, const Vec3& stationInB, 
                         const Vec3& forceInG) const;
    void applyBodyTorque(State& s, int body, const Vec3& torqueInG) const;
    void applyJointForce(State& s, int body, int axis, const Real& r) const;
    
    /// This is a solver which generates internal velocities from spatial ones.
    void velFromCartesian(const Vector& pos, Vector& vel) {assert(false);/*TODO*/}

    void enforceConfigurationConstraints(State&) const;
    void enforceMotionConstraints(State&) const;

    /// Unconstrained (tree) dynamics 
    void calcArticulatedBodyInertias(const State&) const;                        // articulated body inertias
    void calcZ(const State&, const SpatialVecList& spatialForces) const; // articulated body remainder forces
    void calcTreeAccel(const State&) const;                // accels with forces from last calcZ

    void fixVel0(State&, Vector& vel) const; // TODO -- yuck

    /// Part of constrained dynamics (TODO -- more to move here)
    void calcY(const State&) const;


    /// Pass in internal forces in T; they will be adjusted by this routine.
    void calcConstraintCorrectedInternalForces(const State&, Vector& T); 

    const RigidBodyNode& getRigidBodyNode(int nodeNum) const {
        const RigidBodyNodeIndex& ix = nodeNum2NodeMap[nodeNum];
        return *rbNodeLevels[ix.level][ix.offset];
    }
    RigidBodyNode& updRigidBodyNode(int nodeNum) {
        const RigidBodyNodeIndex& ix = nodeNum2NodeMap[nodeNum];
        return *rbNodeLevels[ix.level][ix.offset];
    }

    bool isBuilt() const {return built;}

    // Add a distance constraint equation and assign it a particular multiplier
    // slot to use. Return the assigned distance constraint index for caller's use.
    int addOneDistanceConstraintEquation(
        const RBStation& s1, const RBStation& s2, const Real& d,
        int multIndex);

    //
    //    STATE ARCHEOLOGY
    //    These routines know where the bodies are buried (no pun intended).
    //

    // The ConstructionCache in the State should be a copy of the one
    // we keep locally here. We always use our local copy rather than
    // this one except for checking that the State looks reasonable.
    const SBConstructionCache& getConstructionCache(const State& s) const {
        assert(built && constructionCacheIndex >= 0);
        return Value<SBConstructionCache>::downcast
            (s.getCacheEntry(constructionCacheIndex)).get();
    }
    SBConstructionCache& updConstructionCache(const State& s) const { //mutable
        assert(built && constructionCacheIndex >= 0);
        return Value<SBConstructionCache>::downcast
            (s.updCacheEntry(constructionCacheIndex)).upd();
    }

    const SBModelingCache& getModelingCache(const State& s) const {
        return Value<SBModelingCache>::downcast
            (s.getCacheEntry(constructionCache.modelingCacheIndex)).get();
    }
    SBModelingCache& updModelingCache(const State& s) const { //mutable
        return Value<SBModelingCache>::downcast
            (s.updCacheEntry(constructionCache.modelingCacheIndex)).upd();
    }

    const SBParameterCache& getParameterCache(const State& s) const {
        return Value<SBParameterCache>::downcast
            (s.getCacheEntry(getModelingCache(s).parameterCacheIndex)).get();
    }
    SBParameterCache& updParameterCache(const State& s) const { //mutable
        return Value<SBParameterCache>::downcast
            (s.updCacheEntry(getModelingCache(s).parameterCacheIndex)).upd();
    }

    const SBTimeCache& getTimeCache(const State& s) const {
        return Value<SBTimeCache>::downcast
            (s.getCacheEntry(getModelingCache(s).timeCacheIndex)).get();
    }
    SBTimeCache& updTimeCache(const State& s) const { //mutable
        return Value<SBTimeCache>::downcast
            (s.updCacheEntry(getModelingCache(s).timeCacheIndex)).upd();
    }

    const SBConfigurationCache& getConfigurationCache(const State& s) const {
        return Value<SBConfigurationCache>::downcast
            (s.getCacheEntry(getModelingCache(s).qCacheIndex)).get();
    }
    SBConfigurationCache& updConfigurationCache(const State& s) const { //mutable
        return Value<SBConfigurationCache>::downcast
            (s.updCacheEntry(getModelingCache(s).qCacheIndex)).upd();
    }

    const SBMotionCache& getMotionCache(const State& s) const {
        return Value<SBMotionCache>::downcast
            (s.getCacheEntry(getModelingCache(s).uCacheIndex)).get();
    }
    SBMotionCache& updMotionCache(const State& s) const { //mutable
        return Value<SBMotionCache>::downcast
            (s.updCacheEntry(getModelingCache(s).uCacheIndex)).upd();
    }

    const SBDynamicsCache& getDynamicsCache(const State& s) const {
        return Value<SBDynamicsCache>::downcast
            (s.getCacheEntry(getModelingCache(s).dynamicsCacheIndex)).get();
    }
    SBDynamicsCache& updDynamicsCache(const State& s) const { //mutable
        return Value<SBDynamicsCache>::downcast
            (s.updCacheEntry(getModelingCache(s).dynamicsCacheIndex)).upd();
    }

    const SBReactionCache& getReactionCache(const State& s) const {
        return Value<SBReactionCache>::downcast
            (s.getCacheEntry(getModelingCache(s).reactionCacheIndex)).get();
    }
    SBReactionCache& updReactionCache(const State& s) const { //mutable
        return Value<SBReactionCache>::downcast
            (s.updCacheEntry(getModelingCache(s).reactionCacheIndex)).upd();
    }


    const SBModelingVars& getModelingVars(const State& s) const {
        return Value<SBModelingVars>::downcast
            (s.getDiscreteVariable(constructionCache.modelingVarsIndex)).get();
    }
    SBModelingVars& updModelingVars(State& s) const {
        return Value<SBModelingVars>::downcast
            (s.updDiscreteVariable(constructionCache.modelingVarsIndex)).upd();
    }

    const SBParameterVars& getParameterVars(const State& s) const {
        return Value<SBParameterVars>::downcast
            (s.getDiscreteVariable(getModelingCache(s).parameterVarsIndex)).get();
    }
    SBParameterVars& updParameterVars(State& s) const {
        return Value<SBParameterVars>::downcast
            (s.updDiscreteVariable(getModelingCache(s).parameterVarsIndex)).upd();
    }

    const SBTimeVars& getTimeVars(const State& s) const {
        return Value<SBTimeVars>::downcast
            (s.getDiscreteVariable(getModelingCache(s).timeVarsIndex)).get();
    }
    SBTimeVars& updTimeVars(State& s) const {
        return Value<SBTimeVars>::downcast
            (s.updDiscreteVariable(getModelingCache(s).timeVarsIndex)).upd();
    }

    const SBConfigurationVars& getConfigurationVars(const State& s) const {
        return Value<SBConfigurationVars>::downcast
            (s.getDiscreteVariable(getModelingCache(s).qVarsIndex)).get();
    }
    SBConfigurationVars& updConfigurationVars(State& s) const {
        return Value<SBConfigurationVars>::downcast
            (s.updDiscreteVariable(getModelingCache(s).qVarsIndex)).upd();
    }

    const SBMotionVars& getMotionVars(const State& s) const {
        return Value<SBMotionVars>::downcast
            (s.getDiscreteVariable(getModelingCache(s).uVarsIndex)).get();
    }
    SBMotionVars& updMotionVars(State& s) const {
        return Value<SBMotionVars>::downcast
            (s.updDiscreteVariable(getModelingCache(s).uVarsIndex)).upd();
    }

    const SBDynamicsVars& getDynamicsVars(const State& s) const {
        return Value<SBDynamicsVars>::downcast
            (s.getDiscreteVariable(getModelingCache(s).dynamicsVarsIndex)).get();
    }
    SBDynamicsVars& updDynamicsVars(State& s) const {
        return Value<SBDynamicsVars>::downcast
            (s.updDiscreteVariable(getModelingCache(s).dynamicsVarsIndex)).upd();
    }

    const SBReactionVars& getReactionVars(const State& s) const {
        return Value<SBReactionVars>::downcast
            (s.getDiscreteVariable(getModelingCache(s).reactionVarsIndex)).get();
    }
    SBReactionVars& updReactionVars(State& s) const {
        return Value<SBReactionVars>::downcast
            (s.updDiscreteVariable(getModelingCache(s).reactionVarsIndex)).upd();
    }

    
    // Access to our portion of State arrays.
    void setQ(State& s, const Vector& q) const {
        assert(q.size() == constructionCache.maxNQs);
        updQ(s) = q;
    }
    void setU(State& s, const Vector& u) const {
        assert(u.size() == constructionCache.nDOFs);
        updU(s) = u;
    }

    const VectorView getQ(const State& s) const {
        return s.getQ()(getModelingCache(s).qIndex, constructionCache.maxNQs);
    }
    VectorView updQ(State& s) const {
        return s.updQ()(getModelingCache(s).qIndex, constructionCache.maxNQs);
    }
    const VectorView getQDot(const State& s) const {
        return s.getQDot()(getModelingCache(s).qIndex, constructionCache.maxNQs);
    }
    VectorView updQDot(const State& s) const { // mutable
        return s.updQDot()(getModelingCache(s).qIndex, constructionCache.maxNQs);
    }
    const VectorView getQDotDot(const State& s) const {
        return s.getQDotDot()(getModelingCache(s).qIndex, constructionCache.maxNQs);
    }
    VectorView updQDotDot(const State& s) const { // mutable
        return s.updQDotDot()(getModelingCache(s).qIndex, constructionCache.maxNQs);
    }

    const VectorView getU(const State& s) const {
        return s.getU()(getModelingCache(s).uIndex, constructionCache.nDOFs);
    }
    VectorView updU(State& s) const {
        return s.updU()(getModelingCache(s).uIndex, constructionCache.nDOFs);
    }
    const VectorView getUDot(const State& s) const {
        return s.getUDot()(getModelingCache(s).uIndex, constructionCache.nDOFs);
    }
    VectorView updUDot(const State& s) const { // mutable
        return s.updUDot()(getModelingCache(s).uIndex, constructionCache.nDOFs);
    }

private:
    void addGroundNode();
    int addConstraintNode(ConstraintNode*&);

    // Given a forces in the state, calculate accelerations ignoring
    // constraints, and leave the results in the state. 
    // Must have already called realizeDynamics().
    // We also allow some extra forces to be supplied, with the intent
    // that these will be used to deal with internal forces generated
    // by constraints. 
    void calcTreeForwardDynamics (const State& s,
        const Vector*              extraJointForces,
        const Vector_<SpatialVec>* extraBodyForces) const;

    // Given a set of forces in the state, calculate acclerations resulting from
    // those forces and enforcement of acceleration constraints, and update the state.
    void calcLoopForwardDynamics(const State&) const;

    friend std::ostream& operator<<(std::ostream&, const RigidBodyTree&);
    friend class SimbodySubsystem;

    struct RigidBodyNodeIndex {
        RigidBodyNodeIndex(int l, int o) : level(l), offset(o) { }
        int level, offset;
    };

private:
    // Initialize to 0 at beginning of construction. These are for doling
    // out Q & U state variables to the nodes.
    int nextUSlot;
    int nextUSqSlot;
    int nextQSlot;

    // set by endConstruction
    int DOFTotal;   // summed over all nodes
    int SqDOFTotal; // sum of squares of ndofs per node
    int maxNQTotal; // sum of dofs with room for quaternions
    bool built;

    // set by realizeConstruction
    SBConstructionCache constructionCache;
    int constructionCacheIndex; // constructionCache is copied here in the State

    // This holds pointers to nodes and serves to map (level,offset) to nodeNum.
    Array<RBNodePtrList>      rbNodeLevels;
    // Map nodeNum to (level,offset).
    std::vector<RigidBodyNodeIndex> nodeNum2NodeMap;

    // This holds pointers to the abstract constraint nodes which correspond
    // the the user's idea of constraints in a manner analogous to the
    // linked bodies represented by RigidBodyNodes. Each of these may generate
    // several constraint equations.
    Array<ConstraintNode*>       constraintNodes;
    Array<RBDistanceConstraint*> distanceConstraints;
    
    LengthConstraints* lConstraints;

};

std::ostream& operator<<(std::ostream&, const RigidBodyTree&);

#endif // RIGID_BODY_TREE_H_
