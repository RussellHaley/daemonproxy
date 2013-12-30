/******************************************************************************\
*   Contained_RBTree.h              by: TheSilverDirk / Michael Conrad
*   2000-06-23: Created
*   2005-04-29: Hacked-up sufficiently to be compilable under C.
*   2013-12-30: Made C API more sensible and intuitive
*
*   This is a red/black binary search tree implementation using the
*   "contained class" system, where data structure nodes are contained
*   within the objects that they index.
\******************************************************************************/

#ifndef CONTAINED_RBTREE_H
#define CONTAINED_RBTREE_H

//namespace ContainedClass {

/******************************************************************************\
*   Contained RBTree Data Structure
\******************************************************************************/

#define RBTreeNode_Black 0
#define RBTreeNode_Red 1
#define RBTreeNode_Unassigned 2
typedef struct RBTreeNode_s {
//	enum NodeColor { Black= 0, Red= 1, Unassigned= 2 };

	struct RBTreeNode_s* Left;
	struct RBTreeNode_s* Right;
	struct RBTreeNode_s* Parent;
	int         Color;
	void*       Object;
} RBTreeNode;

typedef int  RBTreeCompareFn( void *Data, RBTreeNode *Node );

typedef struct RBTreeSearch_s {
	RBTreeNode *Nearest;
	int Relation;
} RBTreeSearch;

typedef struct RBTree_s {
	RBTreeNode RootSentinel; // the left child of the sentinel is the root node
	RBTreeCompareFn *Compare;
} RBTree;

/******************************************************************************\
*   Base RBTree functions - all functions required to manipulate a R/B tree.
*
*   These functions are all ordinary functions in order to be compatible with
*     other languages, such as C
\******************************************************************************/

void RBTreeNode_Init( RBTreeNode* Node );
bool RBTreeNode_IsSentinel( RBTreeNode *Node );
RBTreeNode* RBTreeNode_GetPrev( RBTreeNode* Node );
RBTreeNode* RBTreeNode_GetNext( RBTreeNode* Node );
RBTreeNode* RBTreeNode_GetRightmost( RBTreeNode* Node );
RBTreeNode* RBTreeNode_GetLeftmost( RBTreeNode* Node );
void RBTreeNode_LeftSide_LeftRotate( RBTreeNode* Node );
void RBTreeNode_LeftSide_RightRotate( RBTreeNode* Node );
void RBTreeNode_RightSide_RightRotate( RBTreeNode* Node );
void RBTreeNode_RightSide_LeftRotate( RBTreeNode* Node );
void RBTreeNode_Balance( RBTreeNode* Node );
bool RBTreeNode_Prune( RBTreeNode* Node );
void RBTreeNode_PruneLeaf( RBTreeNode* Node );

void RBTree_Init( RBTree *Tree, RBTreeCompareFn *Compare );
void RBTree_Clear( RBTree *Tree );
bool RBTree_Add( RBTree *Tree, RBTreeNode* NewNode, const void* CompareData );
RBTreeSearch RBTree_Find( const RBTree *Tree, const void* CompareData );

extern RBTreeNode Sentinel;

static inline RBTreeNode * RBTree_GetFirst( const RBTree *Tree ) {
	return Tree->RootSentinel.Left == &Sentinel? NULL
		: RBTreeNode_GetLeftmost(Tree->RootSentinel.Left);
}
static inline RBTreeNode * RBTree_GetLast( const RBTree *Tree ) {
	return Tree->RootSentinel.Left == &Sentinel? NULL
		: RBTreeNode_GetRightmost(Tree->RootSentinel.Left);
}

/******************************************************************************\
*   Contained RBTree Class                                                     *
\******************************************************************************/
/*
class ECannotAddNode {};
class ECannotRemoveNode {};

class RBTree {
public:
	class Node: public RBTreeNode {
	public:
		Node()  { RBTreeNode_Init(this); }
		~Node() { if (RBTreeNode::Color != RBTreeNode::Unassigned) RBTree_Prune(this); }

		// The average user shouldn't need these, but they might come in handy.
		void* Left()   const { return RBTreeNode::Left->Object;   }
		void* Right()  const { return RBTreeNode::Right->Object;  }
		void* Parent() const { return RBTreeNode::Parent->Object; }
		int   Color()        { return RBTreeNode::Color; }

		// These let you use your nodes as a sequence.
		void* Next()   const { return RBTree_GetNext(this)->Object; }
		void* Prev()   const { return RBTree_GetPrev(this)->Object; }

		bool IsSentinel() { return RBTreeNode_IsSentinel(this); }

		friend RBTree;
	};

private:
	RBTreeNode RootSentinel; // the left child of the sentinel is the root node
public:
	RBTree_inorder_func *inorder;
	RBTree_compare_func *compare;

	RBTree() { RBTree_InitRootSentinel(RootSentinel); }
	~RBTree() { Clear(); }

	void Clear() { RBTree_Clear(RootSentinel); }
	bool IsEmpty() const { return RBTreeNode_IsSentinel(RootSentinel.Left); }

	void* GetRoot()  const { return RootSentinel.Left->Object; }
	void* GetFirst() const { return RBTree_GetLeftmost(RootSentinel.Left)->Object; }
	void* GetLast()  const { return RBTree_GetRightmost(RootSentinel.Left)->Object; }

	void Add( Node* NewNode ) { if (!RBTree_Add(RootSentinel, NewNode, inorder)) throw ECannotAddNode(); }

	void* Find( const void *SearchKey ) const { return RBTree_Find(RootSentinel, SearchKey, compare)->Object; }

	static void Remove( Node* Node ) { if (!RBTree_Prune(Node)) throw ECannotRemoveNode(); }
};
*/

/******************************************************************************\
*   Type-Safe Contained Red/Black Tree Class                                   *
\******************************************************************************/
/*
template <class T>
class TypedRBTree: public RBTree {
public:
	typedef RBTree Inherited;

	class Node: public RBTree::Node {
	public:
		typedef RBTree::Node Inherited;

		// The average user shouldn't need these, but they might come in handy.
		T* Left()   const { return (T*) Inherited::Left();   }
		T* Right()  const { return (T*) Inherited::Right();  }
		T* Parent() const { return (T*) Inherited::Parent(); }
		int Color() const { return Inherited::Color(); }

		// These let you use your nodes as a sequence.
		T* Next()   const { return (T*) Inherited::Next(); }
		T* Prev()   const { return (T*) Inherited::Prev(); }

		friend TypedRBTree<T>;
	};

public:
	T* GetRoot()  const { return (T*) Inherited::GetRoot();  }
	T* GetFirst() const { return (T*) Inherited::GetFirst(); }
	T* GetLast()  const { return (T*) Inherited::GetLast();  }

	void Add( Node* NewNode ) { Inherited::Add(NewNode); }

	T* Find( const void *Val ) const { return (T*) Inherited::Find(Val); }

	static void Remove( Node* Node ) { Inherited::Remove(Node); }
};

}
*/
#endif

