/*
 * tree234.h: header defining functions in tree234.c.
 */

#ifndef TREE234_H
#define TREE234_H

/*
 * These typedefs are notionally opaque outside tree234.c itself.
 */
typedef struct node234_Tag node234;
typedef struct tree234_Tag tree234;
typedef struct enum234_Tag enum234;

/*
 * enum234 must be declared here because client code needs to be
 * able to create automatic instances of it. This declaration does
 * not constitute licence to use its internals outside tree234.c.
 * The contents of this structure may change without notice. YOU
 * HAVE BEEN WARNED.
 */
struct enum234_Tag {
    node234 *node;
    int posn;
};

typedef int (*cmpfn234)(void *, void *);

/*
 * Create a 2-3-4 tree.
 */
tree234 *newtree234(cmpfn234 cmp);

/*
 * Free a 2-3-4 tree (not including freeing the elements).
 */
void freetree234(tree234 *t);

/*
 * Add an element e to a 2-3-4 tree t. Returns e on success, or if
 * an existing element compares equal, returns that.
 */
void *add234(tree234 *t, void *e);

/*
 * Find an element e in a 2-3-4 tree t. Returns NULL if not found.
 * e is always passed as the first argument to cmp, so cmp can be
 * an asymmetric function if desired. cmp can also be passed as
 * NULL, in which case the compare function from the tree proper
 * will be used.
 */
void *find234(tree234 *t, void *e, cmpfn234 cmp);

/*
 * Delete an element e in a 2-3-4 tree. Does not free the element,
 * merely removes all links to it from the tree nodes.
 */
void del234(tree234 *t, void *e);

/*
 * Iterate over the elements of a tree234, in order.
 *
 *   enum234 e;
 *   for (p = first234(tree, &e); p; p = next234(&e)) consume(p);
 */
void *first234(tree234 *t, enum234 *e);
void *next234(enum234 *e);

#endif /* TREE234_H */
