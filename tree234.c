/*
 * tree234.c: reasonably generic 2-3-4 tree routines. Currently
 * supports insert, delete, find and iterate operations.
 */

#include <stdio.h>
#include <stdlib.h>

#include "tree234.h"

#define mknew(typ) ( (typ *) malloc (sizeof (typ)) )
#define sfree free

#ifdef TEST
#define LOG(x) (printf x)
#else
#define LOG(x)
#endif

struct tree234_Tag {
    node234 *root;
    cmpfn234 cmp;
};

struct node234_Tag {
    node234 *parent;
    node234 *kids[4];
    void *elems[3];
};

/*
 * Create a 2-3-4 tree.
 */
tree234 *newtree234(cmpfn234 cmp) {
    tree234 *ret = mknew(tree234);
    LOG(("created tree %p\n", ret));
    ret->root = NULL;
    ret->cmp = cmp;
    return ret;
}

/*
 * Free a 2-3-4 tree (not including freeing the elements).
 */
static void freenode234(node234 *n) {
    if (!n)
	return;
    freenode234(n->kids[0]);
    freenode234(n->kids[1]);
    freenode234(n->kids[2]);
    freenode234(n->kids[3]);
    sfree(n);
}
void freetree234(tree234 *t) {
    freenode234(t->root);
    sfree(t);
}

/*
 * Add an element e to a 2-3-4 tree t. Returns e on success, or if
 * an existing element compares equal, returns that.
 */
void *add234(tree234 *t, void *e) {
    node234 *n, **np, *left, *right;
    void *orig_e = e;
    int c;

    LOG(("adding node %p to tree %p\n", e, t));
    if (t->root == NULL) {
	t->root = mknew(node234);
	t->root->elems[1] = t->root->elems[2] = NULL;
	t->root->kids[0] = t->root->kids[1] = NULL;
	t->root->kids[2] = t->root->kids[3] = NULL;
	t->root->parent = NULL;
	t->root->elems[0] = e;
	LOG(("  created root %p\n", t->root));
	return orig_e;
    }

    np = &t->root;
    while (*np) {
	n = *np;
	LOG(("  node %p: %p [%p] %p [%p] %p [%p] %p\n",
	     n, n->kids[0], n->elems[0], n->kids[1], n->elems[1],
	     n->kids[2], n->elems[2], n->kids[3]));
	if ((c = t->cmp(e, n->elems[0])) < 0)
	    np = &n->kids[0];
	else if (c == 0)
	    return n->elems[0];	       /* already exists */
	else if (n->elems[1] == NULL || (c = t->cmp(e, n->elems[1])) < 0)
	    np = &n->kids[1];
	else if (c == 0)
	    return n->elems[1];	       /* already exists */
	else if (n->elems[2] == NULL || (c = t->cmp(e, n->elems[2])) < 0)
	    np = &n->kids[2];
	else if (c == 0)
	    return n->elems[2];	       /* already exists */
	else
	    np = &n->kids[3];
	LOG(("  moving to child %d (%p)\n", np - n->kids, *np));
    }

    /*
     * We need to insert the new element in n at position np.
     */
    left = NULL;
    right = NULL;
    while (n) {
	LOG(("  at %p: %p [%p] %p [%p] %p [%p] %p\n",
	     n, n->kids[0], n->elems[0], n->kids[1], n->elems[1],
	     n->kids[2], n->elems[2], n->kids[3]));
	LOG(("  need to insert %p [%p] %p at position %d\n",
	     left, e, right, np - n->kids));
	if (n->elems[1] == NULL) {
	    /*
	     * Insert in a 2-node; simple.
	     */
	    if (np == &n->kids[0]) {
		LOG(("  inserting on left of 2-node\n"));
		n->kids[2] = n->kids[1];
		n->elems[1] = n->elems[0];
		n->kids[1] = right;
		n->elems[0] = e;
		n->kids[0] = left;
	    } else { /* np == &n->kids[1] */
		LOG(("  inserting on right of 2-node\n"));
		n->kids[2] = right;
		n->elems[1] = e;
		n->kids[1] = left;
	    }
	    if (n->kids[0]) n->kids[0]->parent = n;
	    if (n->kids[1]) n->kids[1]->parent = n;
	    if (n->kids[2]) n->kids[2]->parent = n;
	    LOG(("  done\n"));
	    break;
	} else if (n->elems[2] == NULL) {
	    /*
	     * Insert in a 3-node; simple.
	     */
	    if (np == &n->kids[0]) {
		LOG(("  inserting on left of 3-node\n"));
		n->kids[3] = n->kids[2];
		n->elems[2] = n->elems[1];
		n->kids[2] = n->kids[1];
		n->elems[1] = n->elems[0];
		n->kids[1] = right;
		n->elems[0] = e;
		n->kids[0] = left;
	    } else if (np == &n->kids[1]) {
		LOG(("  inserting in middle of 3-node\n"));
		n->kids[3] = n->kids[2];
		n->elems[2] = n->elems[1];
		n->kids[2] = right;
		n->elems[1] = e;
		n->kids[1] = left;
	    } else { /* np == &n->kids[2] */
		LOG(("  inserting on right of 3-node\n"));
		n->kids[3] = right;
		n->elems[2] = e;
		n->kids[2] = left;
	    }
	    if (n->kids[0]) n->kids[0]->parent = n;
	    if (n->kids[1]) n->kids[1]->parent = n;
	    if (n->kids[2]) n->kids[2]->parent = n;
	    if (n->kids[3]) n->kids[3]->parent = n;
	    LOG(("  done\n"));
	    break;
	} else {
	    node234 *m = mknew(node234);
	    m->parent = n->parent;
	    LOG(("  splitting a 4-node; created new node %p\n", m));
	    /*
	     * Insert in a 4-node; split into a 2-node and a
	     * 3-node, and move focus up a level.
	     * 
	     * I don't think it matters which way round we put the
	     * 2 and the 3. For simplicity, we'll put the 3 first
	     * always.
	     */
	    if (np == &n->kids[0]) {
		m->kids[0] = left;
		m->elems[0] = e;
		m->kids[1] = right;
		m->elems[1] = n->elems[0];
		m->kids[2] = n->kids[1];
		e = n->elems[1];
		n->kids[0] = n->kids[2];
		n->elems[0] = n->elems[2];
		n->kids[1] = n->kids[3];
	    } else if (np == &n->kids[1]) {
		m->kids[0] = n->kids[0];
		m->elems[0] = n->elems[0];
		m->kids[1] = left;
		m->elems[1] = e;
		m->kids[2] = right;
		e = n->elems[1];
		n->kids[0] = n->kids[2];
		n->elems[0] = n->elems[2];
		n->kids[1] = n->kids[3];
	    } else if (np == &n->kids[2]) {
		m->kids[0] = n->kids[0];
		m->elems[0] = n->elems[0];
		m->kids[1] = n->kids[1];
		m->elems[1] = n->elems[1];
		m->kids[2] = left;
		/* e = e; */
		n->kids[0] = right;
		n->elems[0] = n->elems[2];
		n->kids[1] = n->kids[3];
	    } else { /* np == &n->kids[3] */
		m->kids[0] = n->kids[0];
		m->elems[0] = n->elems[0];
		m->kids[1] = n->kids[1];
		m->elems[1] = n->elems[1];
		m->kids[2] = n->kids[2];
		n->kids[0] = left;
		n->elems[0] = e;
		n->kids[1] = right;
		e = n->elems[2];
	    }
	    m->kids[3] = n->kids[3] = n->kids[2] = NULL;
	    m->elems[2] = n->elems[2] = n->elems[1] = NULL;
	    if (m->kids[0]) m->kids[0]->parent = m;
	    if (m->kids[1]) m->kids[1]->parent = m;
	    if (m->kids[2]) m->kids[2]->parent = m;
	    if (n->kids[0]) n->kids[0]->parent = n;
	    if (n->kids[1]) n->kids[1]->parent = n;
	    LOG(("  left (%p): %p [%p] %p [%p] %p\n", m,
		 m->kids[0], m->elems[0],
		 m->kids[1], m->elems[1],
		 m->kids[2]));
	    LOG(("  right (%p): %p [%p] %p\n", n,
		 n->kids[0], n->elems[0],
		 n->kids[1]));
	    left = m;
	    right = n;
	}
	if (n->parent)
	    np = (n->parent->kids[0] == n ? &n->parent->kids[0] :
		  n->parent->kids[1] == n ? &n->parent->kids[1] :
		  n->parent->kids[2] == n ? &n->parent->kids[2] :
		  &n->parent->kids[3]);
	n = n->parent;
    }

    /*
     * If we've come out of here by `break', n will still be
     * non-NULL and we've finished. If we've come here because n is
     * NULL, we need to create a new root for the tree because the
     * old one has just split into two.
     */
    if (!n) {
	LOG(("  root is overloaded, split into two\n"));
	t->root = mknew(node234);
	t->root->kids[0] = left;
	t->root->elems[0] = e;
	t->root->kids[1] = right;
	t->root->elems[1] = NULL;
	t->root->kids[2] = NULL;
	t->root->elems[2] = NULL;
	t->root->kids[3] = NULL;
	t->root->parent = NULL;
	if (t->root->kids[0]) t->root->kids[0]->parent = t->root;
	if (t->root->kids[1]) t->root->kids[1]->parent = t->root;
	LOG(("  new root is %p [%p] %p\n",
	     t->root->kids[0], t->root->elems[0], t->root->kids[1]));
    }

    return orig_e;
}

/*
 * Find an element e in a 2-3-4 tree t. Returns NULL if not found.
 * e is always passed as the first argument to cmp, so cmp can be
 * an asymmetric function if desired. cmp can also be passed as
 * NULL, in which case the compare function from the tree proper
 * will be used.
 */
void *find234(tree234 *t, void *e, cmpfn234 cmp) {
    node234 *n;
    int c;

    if (t->root == NULL)
	return NULL;

    if (cmp == NULL)
	cmp = t->cmp;

    n = t->root;
    while (n) {
	if ( (c = cmp(e, n->elems[0])) < 0)
	    n = n->kids[0];
	else if (c == 0)
	    return n->elems[0];
	else if (n->elems[1] == NULL || (c = cmp(e, n->elems[1])) < 0)
	    n = n->kids[1];
	else if (c == 0)
	    return n->elems[1];
	else if (n->elems[2] == NULL || (c = cmp(e, n->elems[2])) < 0)
	    n = n->kids[2];
	else if (c == 0)
	    return n->elems[2];
	else
	    n = n->kids[3];
    }

    /*
     * We've found our way to the bottom of the tree and we know
     * where we would insert this node if we wanted to. But it
     * isn't there.
     */
    return NULL;
}

/*
 * Delete an element e in a 2-3-4 tree. Does not free the element,
 * merely removes all links to it from the tree nodes.
 */
void del234(tree234 *t, void *e) {
    node234 *n;
    int ei = -1;

    n = t->root;
    LOG(("deleting %p from tree %p\n", e, t));
    while (1) {
	while (n) {
	    int c;
	    int ki;
	    node234 *sub;

	    LOG(("  node %p: %p [%p] %p [%p] %p [%p] %p\n",
		 n, n->kids[0], n->elems[0], n->kids[1], n->elems[1],
		 n->kids[2], n->elems[2], n->kids[3]));	
	    if ((c = t->cmp(e, n->elems[0])) < 0) {
		ki = 0;
	    } else if (c == 0) {
		ei = 0; break;
	    } else if (n->elems[1] == NULL || (c = t->cmp(e, n->elems[1])) < 0) {
		ki = 1;
	    } else if (c == 0) {
		ei = 1; break;
	    } else if (n->elems[2] == NULL || (c = t->cmp(e, n->elems[2])) < 0) {
		ki = 2;
	    } else if (c == 0) {
		ei = 2; break;
	    } else {
		ki = 3;
	    }
	    /*
	     * Recurse down to subtree ki. If it has only one element,
	     * we have to do some transformation to start with.
	     */
	    LOG(("  moving to subtree %d\n", ki));
	    sub = n->kids[ki];
	    if (!sub->elems[1]) {
		LOG(("  subtree has only one element!\n", ki));
		if (ki > 0 && n->kids[ki-1]->elems[1]) {
		    /*
		     * Case 3a, left-handed variant. Child ki has
		     * only one element, but child ki-1 has two or
		     * more. So we need to move a subtree from ki-1
		     * to ki.
		     * 
		     *                . C .                     . B .
		     *               /     \     ->            /     \
		     * [more] a A b B c   d D e      [more] a A b   c C d D e
		     */
		    node234 *sib = n->kids[ki-1];
		    int lastelem = (sib->elems[2] ? 2 :
				    sib->elems[1] ? 1 : 0);
		    sub->kids[2] = sub->kids[1];
		    sub->elems[1] = sub->elems[0];
		    sub->kids[1] = sub->kids[0];
		    sub->elems[0] = n->elems[ki-1];
		    sub->kids[0] = sib->kids[lastelem+1];
		    n->elems[ki-1] = sib->elems[lastelem];
		    sib->kids[lastelem+1] = NULL;
		    sib->elems[lastelem] = NULL;
		    LOG(("  case 3a left\n"));
		} else if (ki < 3 && n->kids[ki+1] &&
			   n->kids[ki+1]->elems[1]) {
		    /*
		     * Case 3a, right-handed variant. ki has only
		     * one element but ki+1 has two or more. Move a
		     * subtree from ki+1 to ki.
		     * 
		     *      . B .                             . C .
		     *     /     \                ->         /     \
		     *  a A b   c C d D e [more]      a A b B c   d D e [more]
		     */
		    node234 *sib = n->kids[ki+1];
		    int j;
		    sub->elems[1] = n->elems[ki];
		    sub->kids[2] = sib->kids[0];
		    n->elems[ki] = sib->elems[0];
		    sib->kids[0] = sib->kids[1];
		    for (j = 0; j < 2 && sib->elems[j+1]; j++) {
			sib->kids[j+1] = sib->kids[j+2];
			sib->elems[j] = sib->elems[j+1];
		    }
		    sib->kids[j+1] = NULL;
		    sib->elems[j] = NULL;
		    LOG(("  case 3a right\n"));
		} else {
		    /*
		     * Case 3b. ki has only one element, and has no
		     * neighbour with more than one. So pick a
		     * neighbour and merge it with ki, taking an
		     * element down from n to go in the middle.
		     *
		     *      . B .                .
		     *     /     \     ->        |
		     *  a A b   c C d      a A b B c C d
		     * 
		     * (Since at all points we have avoided
		     * descending to a node with only one element,
		     * we can be sure that n is not reduced to
		     * nothingness by this move, _unless_ it was
		     * the very first node, ie the root of the
		     * tree. In that case we remove the now-empty
		     * root and replace it with its single large
		     * child as shown.)
		     */
		    node234 *sib;
		    int j;

		    if (ki > 0)
			ki--;
		    sib = n->kids[ki];
		    sub = n->kids[ki+1];

		    sub->kids[3] = sub->kids[1];
		    sub->elems[2] = sub->elems[0];
		    sub->kids[2] = sub->kids[0];
		    sub->elems[1] = n->elems[ki];
		    sub->kids[1] = sib->kids[1];
		    sub->elems[0] = sib->elems[0];
		    sub->kids[0] = sib->kids[0];

		    sfree(sib);

		    /*
		     * That's built the big node in sub. Now we
		     * need to remove the reference to sib in n.
		     */
		    for (j = ki; j < 3 && n->kids[j+1]; j++) {
			n->kids[j] = n->kids[j+1];
			n->elems[j] = j<2 ? n->elems[j+1] : NULL;
		    }
		    n->kids[j] = NULL;
		    if (j < 3) n->elems[j] = NULL;
		    LOG(("  case 3b\n"));

		    if (!n->elems[0]) {
			/*
			 * The root is empty and needs to be
			 * removed.
			 */
			LOG(("  shifting root!\n"));
			t->root = sub;
			sub->parent = NULL;
			sfree(n);
		    }
		}
	    }
	    n = sub;
	}
	if (ei==-1)
	    return;		       /* nothing to do; `already removed' */

	/*
	 * Treat special case: this is the one remaining item in
	 * the tree. n is the tree root (no parent), has one
	 * element (no elems[1]), and has no kids (no kids[0]).
	 */
	if (!n->parent && !n->elems[1] && !n->kids[0]) {
	    LOG(("  removed last element in tree\n"));
	    sfree(n);
	    t->root = NULL;
	    return;
	}

	/*
	 * Now we have the element we want, as n->elems[ei], and we
	 * have also arranged for that element not to be the only
	 * one in its node. So...
	 */

	if (!n->kids[0] && n->elems[1]) {
	    /*
	     * Case 1. n is a leaf node with more than one element,
	     * so it's _really easy_. Just delete the thing and
	     * we're done.
	     */
	    int i;
	    LOG(("  case 1\n"));
	    for (i = ei; i < 2 && n->elems[i+1]; i++)
		n->elems[i] = n->elems[i+1];
	    n->elems[i] = NULL;
	    return;		       /* finished! */
	} else if (n->kids[ei]->elems[1]) {
	    /*
	     * Case 2a. n is an internal node, and the root of the
	     * subtree to the left of e has more than one element.
	     * So find the predecessor p to e (ie the largest node
	     * in that subtree), place it where e currently is, and
	     * then start the deletion process over again on the
	     * subtree with p as target.
	     */
	    node234 *m = n->kids[ei];
	    void *target;
	    LOG(("  case 2a\n"));
	    while (m->kids[0]) {
		m = (m->kids[3] ? m->kids[3] :
		     m->kids[2] ? m->kids[2] :
		     m->kids[1] ? m->kids[1] : m->kids[0]);		     
	    }
	    target = (m->elems[2] ? m->elems[2] :
		      m->elems[1] ? m->elems[1] : m->elems[0]);
	    n->elems[ei] = target;
	    n = n->kids[ei];
	    e = target;
	} else if (n->kids[ei+1]->elems[1]) {
	    /*
	     * Case 2b, symmetric to 2a but s/left/right/ and
	     * s/predecessor/successor/. (And s/largest/smallest/).
	     */
	    node234 *m = n->kids[ei+1];
	    void *target;
	    LOG(("  case 2b\n"));
	    while (m->kids[0]) {
		m = m->kids[0];
	    }
	    target = m->elems[0];
	    n->elems[ei] = target;
	    n = n->kids[ei+1];
	    e = target;
	} else {
	    /*
	     * Case 2c. n is an internal node, and the subtrees to
	     * the left and right of e both have only one element.
	     * So combine the two subnodes into a single big node
	     * with their own elements on the left and right and e
	     * in the middle, then restart the deletion process on
	     * that subtree, with e still as target.
	     */
	    node234 *a = n->kids[ei], *b = n->kids[ei+1];
	    int j;

	    LOG(("  case 2c\n"));
	    a->elems[1] = n->elems[ei];
	    a->kids[2] = b->kids[0];
	    a->elems[2] = b->elems[0];
	    a->kids[3] = b->kids[1];
	    sfree(b);
	    /*
	     * That's built the big node in a, and destroyed b. Now
	     * remove the reference to b (and e) in n.
	     */
	    for (j = ei; j < 2 && n->elems[j+1]; j++) {
		n->elems[j] = n->elems[j+1];
		n->kids[j+1] = n->kids[j+2];
	    }
	    n->elems[j] = NULL;
	    n->kids[j+1] = NULL;
	    /*
	     * Now go round the deletion process again, with n
	     * pointing at the new big node and e still the same.
	     */
	    n = a;
	}
    }
}

/*
 * Iterate over the elements of a tree234, in order.
 */
void *first234(tree234 *t, enum234 *e) {
    node234 *n = t->root;
    if (!n)
	return NULL;
    while (n->kids[0])
	n = n->kids[0];
    e->node = n;
    e->posn = 0;
    return n->elems[0];
}

void *next234(enum234 *e) {
    node234 *n = e->node;
    int pos = e->posn;

    if (n->kids[pos+1]) {
	n = n->kids[pos+1];
	while (n->kids[0])
	    n = n->kids[0];
	e->node = n;
	e->posn = 0;
	return n->elems[0];
    }

    if (pos < 2 && n->elems[pos+1]) {
	e->posn = pos+1;
	return n->elems[e->posn];
    }

    do {
	node234 *nn = n->parent;
	if (nn == NULL)
	    return NULL;	       /* end of tree */
	pos = (nn->kids[0] == n ? 0 :
	       nn->kids[1] == n ? 1 :
	       nn->kids[2] == n ? 2 : 3);
	n = nn;
    } while (pos == 3 || n->kids[pos+1] == NULL);

    e->node = n;
    e->posn = pos;
    return n->elems[pos];
}

#ifdef TEST

int pnode(node234 *n, int level) {
    printf("%*s%p\n", level*4, "", n);
    if (n->kids[0]) pnode(n->kids[0], level+1);
    if (n->elems[0]) printf("%*s\"%s\"\n", level*4+4, "", n->elems[0]);
    if (n->kids[1]) pnode(n->kids[1], level+1);
    if (n->elems[1]) printf("%*s\"%s\"\n", level*4+4, "", n->elems[1]);
    if (n->kids[2]) pnode(n->kids[2], level+1);
    if (n->elems[2]) printf("%*s\"%s\"\n", level*4+4, "", n->elems[2]);
    if (n->kids[3]) pnode(n->kids[3], level+1);
}
int ptree(tree234 *t) {
    if (t->root)
	pnode(t->root, 0);
    else
	printf("empty tree\n");
}

int cmp(void *av, void *bv) {
    char *a = (char *)av;
    char *b = (char *)bv;
    return strcmp(a, b);
}

int main(void) {
    tree234 *t = newtree234(cmp);
    
    add234(t, "Richard");
    add234(t, "Of");
    add234(t, "York");
    add234(t, "Gave");
    add234(t, "Battle");
    add234(t, "In");
    add234(t, "Vain");
    add234(t, "Rabbits");
    add234(t, "On");
    add234(t, "Your");
    add234(t, "Garden");
    add234(t, "Bring");
    add234(t, "Invisible");
    add234(t, "Vegetables");

    ptree(t);
    del234(t, find234(t, "Richard", NULL));
    ptree(t);
    del234(t, find234(t, "Of", NULL));
    ptree(t);
    del234(t, find234(t, "York", NULL));
    ptree(t);
    del234(t, find234(t, "Gave", NULL));
    ptree(t);
    del234(t, find234(t, "Battle", NULL));
    ptree(t);
    del234(t, find234(t, "In", NULL));
    ptree(t);
    del234(t, find234(t, "Vain", NULL));
    ptree(t);
    del234(t, find234(t, "Rabbits", NULL));
    ptree(t);
    del234(t, find234(t, "On", NULL));
    ptree(t);
    del234(t, find234(t, "Your", NULL));
    ptree(t);
    del234(t, find234(t, "Garden", NULL));
    ptree(t);
    del234(t, find234(t, "Bring", NULL));
    ptree(t);
    del234(t, find234(t, "Invisible", NULL));
    ptree(t);
    del234(t, find234(t, "Vegetables", NULL));
    ptree(t);
}
#endif
