/*
 * gtkpanel.h - header file for a panel-based widget container,
 * which holds a number of widgets of which at most one is ever
 * visible at a time, and sizes itself to the maximum of its
 * children's potential size requests.
 */

#ifndef PANELS_H
#define PANELS_H

#include <gdk/gdk.h>
#include <gtk/gtkcontainer.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define TYPE_PANELS (panels_get_type())
#define PANELS(obj) (GTK_CHECK_CAST((obj), TYPE_PANELS, Panels))
#define PANELS_CLASS(klass) \
                (GTK_CHECK_CLASS_CAST((klass), TYPE_PANELS, PanelsClass))
#define IS_PANELS(obj) (GTK_CHECK_TYPE((obj), TYPE_PANELS))
#define IS_PANELS_CLASS(klass) (GTK_CHECK_CLASS_TYPE((klass), TYPE_PANELS))

typedef struct Panels_tag Panels;
typedef struct PanelsClass_tag PanelsClass;
typedef struct PanelsChild_tag PanelsChild;

struct Panels_tag {
    GtkContainer container;
    /* private after here */
    GList *children;		       /* this just holds GtkWidgets */
};

struct PanelsClass_tag {
    GtkContainerClass parent_class;
};

GtkType panels_get_type(void);
GtkWidget *panels_new(void);
void panels_add(Panels *panels, GtkWidget *child);
void panels_switch_to(Panels *panels, GtkWidget *child);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* PANELS_H */
