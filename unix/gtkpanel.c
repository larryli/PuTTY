/*
 * gtkpanel.c - implementation of the `Panels' GTK layout container.
 */

#include "gtkpanel.h"

static void panels_init(Panels *panels);
static void panels_class_init(PanelsClass *klass);
static void panels_map(GtkWidget *widget);
static void panels_unmap(GtkWidget *widget);
static void panels_draw(GtkWidget *widget, GdkRectangle *area);
static gint panels_expose(GtkWidget *widget, GdkEventExpose *event);
static void panels_base_add(GtkContainer *container, GtkWidget *widget);
static void panels_remove(GtkContainer *container, GtkWidget *widget);
static void panels_forall(GtkContainer *container, gboolean include_internals,
                           GtkCallback callback, gpointer callback_data);
static GtkType panels_child_type(GtkContainer *container);
static void panels_size_request(GtkWidget *widget, GtkRequisition *req);
static void panels_size_allocate(GtkWidget *widget, GtkAllocation *alloc);

static GtkContainerClass *parent_class = NULL;

GtkType panels_get_type(void)
{
    static GtkType panels_type = 0;

    if (!panels_type) {
        static const GtkTypeInfo panels_info = {
            "Panels",
            sizeof(Panels),
            sizeof(PanelsClass),
            (GtkClassInitFunc) panels_class_init,
            (GtkObjectInitFunc) panels_init,
            /* reserved_1 */ NULL,
            /* reserved_2 */ NULL,
            (GtkClassInitFunc) NULL,
        };

        panels_type = gtk_type_unique(GTK_TYPE_CONTAINER, &panels_info);
    }

    return panels_type;
}

static void panels_class_init(PanelsClass *klass)
{
    GtkObjectClass *object_class;
    GtkWidgetClass *widget_class;
    GtkContainerClass *container_class;

    object_class = (GtkObjectClass *)klass;
    widget_class = (GtkWidgetClass *)klass;
    container_class = (GtkContainerClass *)klass;

    parent_class = gtk_type_class(GTK_TYPE_CONTAINER);

    /*
     * FIXME: do we have to do all this faffing with set_arg,
     * get_arg and child_arg_type? Ick.
     */

    widget_class->map = panels_map;
    widget_class->unmap = panels_unmap;
    widget_class->draw = panels_draw;
    widget_class->expose_event = panels_expose;
    widget_class->size_request = panels_size_request;
    widget_class->size_allocate = panels_size_allocate;

    container_class->add = panels_base_add;
    container_class->remove = panels_remove;
    container_class->forall = panels_forall;
    container_class->child_type = panels_child_type;
}

static void panels_init(Panels *panels)
{
    GTK_WIDGET_SET_FLAGS(panels, GTK_NO_WINDOW);

    panels->children = NULL;
}

/*
 * These appear to be thoroughly tedious functions; the only reason
 * we have to reimplement them at all is because we defined our own
 * format for our GList of children...
 */
static void panels_map(GtkWidget *widget)
{
    Panels *panels;
    GtkWidget *child;
    GList *children;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(IS_PANELS(widget));

    panels = PANELS(widget);
    GTK_WIDGET_SET_FLAGS(panels, GTK_MAPPED);

    for (children = panels->children;
         children && (child = children->data);
         children = children->next) {
        if (child &&
	    GTK_WIDGET_VISIBLE(child) &&
            !GTK_WIDGET_MAPPED(child))
            gtk_widget_map(child);
    }
}
static void panels_unmap(GtkWidget *widget)
{
    Panels *panels;
    GtkWidget *child;
    GList *children;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(IS_PANELS(widget));

    panels = PANELS(widget);
    GTK_WIDGET_UNSET_FLAGS(panels, GTK_MAPPED);

    for (children = panels->children;
         children && (child = children->data);
         children = children->next) {
        if (child &&
	    GTK_WIDGET_VISIBLE(child) &&
            GTK_WIDGET_MAPPED(child))
            gtk_widget_unmap(child);
    }
}
static void panels_draw(GtkWidget *widget, GdkRectangle *area)
{
    Panels *panels;
    GtkWidget *child;
    GList *children;
    GdkRectangle child_area;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(IS_PANELS(widget));

    if (GTK_WIDGET_DRAWABLE(widget)) {
        panels = PANELS(widget);

        for (children = panels->children;
             children && (child = children->data);
             children = children->next) {
            if (child &&
		GTK_WIDGET_DRAWABLE(child) &&
                gtk_widget_intersect(child, area, &child_area))
                gtk_widget_draw(child, &child_area);
        }
    }
}
static gint panels_expose(GtkWidget *widget, GdkEventExpose *event)
{
    Panels *panels;
    GtkWidget *child;
    GList *children;
    GdkEventExpose child_event;

    g_return_val_if_fail(widget != NULL, FALSE);
    g_return_val_if_fail(IS_PANELS(widget), FALSE);
    g_return_val_if_fail(event != NULL, FALSE);

    if (GTK_WIDGET_DRAWABLE(widget)) {
        panels = PANELS(widget);
        child_event = *event;

        for (children = panels->children;
             children && (child = children->data);
             children = children->next) {
            if (child &&
		GTK_WIDGET_DRAWABLE(child) &&
                GTK_WIDGET_NO_WINDOW(child) &&
                gtk_widget_intersect(child, &event->area,
                                     &child_event.area))
                gtk_widget_event(child, (GdkEvent *)&child_event);
        }
    }
    return FALSE;
}

static void panels_base_add(GtkContainer *container, GtkWidget *widget)
{
    Panels *panels;

    g_return_if_fail(container != NULL);
    g_return_if_fail(IS_PANELS(container));
    g_return_if_fail(widget != NULL);

    panels = PANELS(container);

    panels_add(panels, widget);
}

static void panels_remove(GtkContainer *container, GtkWidget *widget)
{
    Panels *panels;
    GtkWidget *child;
    GList *children;
    gboolean was_visible;

    g_return_if_fail(container != NULL);
    g_return_if_fail(IS_PANELS(container));
    g_return_if_fail(widget != NULL);

    panels = PANELS(container);

    for (children = panels->children;
         children && (child = children->data);
         children = children->next) {
        if (child != widget)
            continue;

        was_visible = GTK_WIDGET_VISIBLE(widget);
        gtk_widget_unparent(widget);
        panels->children = g_list_remove_link(panels->children, children);
        g_list_free(children);
        if (was_visible)
            gtk_widget_queue_resize(GTK_WIDGET(container));
        break;
    }
}

static void panels_forall(GtkContainer *container, gboolean include_internals,
                           GtkCallback callback, gpointer callback_data)
{
    Panels *panels;
    GtkWidget *child;
    GList *children;

    g_return_if_fail(container != NULL);
    g_return_if_fail(IS_PANELS(container));
    g_return_if_fail(callback != NULL);

    panels = PANELS(container);

    for (children = panels->children;
         children && (child = children->data);
         children = children->next)
	if (child)
	    callback(child, callback_data);
}

static GtkType panels_child_type(GtkContainer *container)
{
    return GTK_TYPE_WIDGET;
}

GtkWidget *panels_new(void)
{
    Panels *panels;

    panels = gtk_type_new(panels_get_type());

    return GTK_WIDGET(panels);
}

void panels_add(Panels *panels, GtkWidget *child)
{
    g_return_if_fail(panels != NULL);
    g_return_if_fail(IS_PANELS(panels));
    g_return_if_fail(child != NULL);
    g_return_if_fail(child->parent == NULL);

    panels->children = g_list_append(panels->children, child);

    gtk_widget_set_parent(child, GTK_WIDGET(panels));

    if (GTK_WIDGET_REALIZED(panels))
        gtk_widget_realize(child);

    if (GTK_WIDGET_VISIBLE(panels)) {
        if (GTK_WIDGET_MAPPED(panels))
            gtk_widget_map(child);
        gtk_widget_queue_resize(child);
    }
}

void panels_switch_to(Panels *panels, GtkWidget *target)
{
    GtkWidget *child;
    GList *children;
    gboolean changed;

    g_return_if_fail(panels != NULL);
    g_return_if_fail(IS_PANELS(panels));
    g_return_if_fail(target != NULL);
    g_return_if_fail(target->parent == GTK_WIDGET(panels));

    for (children = panels->children;
         children && (child = children->data);
         children = children->next) {

	if (!child)
            continue;

        if (child == target) {
            if (!GTK_WIDGET_VISIBLE(child)) {
                gtk_widget_show(child);
                changed = TRUE;
            }
        } else {
            if (GTK_WIDGET_VISIBLE(child)) {
                gtk_widget_hide(child);
                changed = TRUE;
            }
        }
    }
    if (changed)
        gtk_widget_queue_resize(child);
}

/*
 * Now here comes the interesting bit. The actual layout part is
 * done in the following two functions:
 * 
 * panels_size_request() examines the list of widgets held in the
 * Panels, and returns a requisition stating the absolute minimum
 * size it can bear to be.
 * 
 * panels_size_allocate() is given an allocation telling it what
 * size the whole container is going to be, and it calls
 * gtk_widget_size_allocate() on all of its (visible) children to
 * set their size and position relative to the top left of the
 * container.
 */

static void panels_size_request(GtkWidget *widget, GtkRequisition *req)
{
    Panels *panels;
    GtkWidget *child;
    GList *children;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(IS_PANELS(widget));
    g_return_if_fail(req != NULL);

    panels = PANELS(widget);

    req->width = 0;
    req->height = 0;

    for (children = panels->children;
         children && (child = children->data);
         children = children->next) {
        GtkRequisition creq;

        gtk_widget_size_request(child, &creq);
        if (req->width < creq.width)
            req->width = creq.width;
        if (req->height < creq.height)
            req->height = creq.height;
    }

    req->width += 2*GTK_CONTAINER(panels)->border_width;
    req->height += 2*GTK_CONTAINER(panels)->border_width;
}

static void panels_size_allocate(GtkWidget *widget, GtkAllocation *alloc)
{
    Panels *panels;
    GtkWidget *child;
    GList *children;
    gint border;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(IS_PANELS(widget));
    g_return_if_fail(alloc != NULL);

    panels = PANELS(widget);
    widget->allocation = *alloc;
    border = GTK_CONTAINER(panels)->border_width;

    for (children = panels->children;
         children && (child = children->data);
         children = children->next) {
        GtkAllocation call;

        /* Only take visible widgets into account. */
        if (!GTK_WIDGET_VISIBLE(child))
            continue;

        call.x = alloc->x + border;
        call.width = alloc->width - 2*border;
        call.y = alloc->y + border;
        call.height = alloc->height - 2*border;

        gtk_widget_size_allocate(child, &call);
    }
}
