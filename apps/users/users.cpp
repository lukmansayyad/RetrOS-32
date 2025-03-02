#include <utils/Graphics.hpp>
#include <utils/StdLib.hpp>
#include <utils/Widgets.hpp>
#include <utils/Thread.hpp>
#include <libc.h>
#include <colors.h>

class UserEditor : public Window {

public:
    UserEditor(int width, int height) : Window(200, 200, "User Editor", 1) {
        this->width = width;
        this->height = height;

        /* Create widgets */
        widgets = new WidgetManager();
        widgets->addWidget(new Button(10, 10, 100, 12, "Button", []() {
            printf("Button pressed!\n");
        }));
    }

    int eventHandler(struct gfx_event* event) {
        switch (event->event)
        {
        case GFX_EVENT_RESOLUTION:
            /* update screensize */
            break;
        case GFX_EVENT_EXIT:
            delete widgets;
            return -1;
        case GFX_EVENT_KEYBOARD:
            widgets->Keyboard(event->data);
            /* keyboard event in e.data */
            break;
        case GFX_EVENT_MOUSE:
            widgets->Mouse(event->data, event->data2);
            break;
        }
        return 0;
    }

    void draw() {
        /* Clear screen */
        drawRect(0, 0, width, height, 30);
        /* Draw widgets */
        widgets->draw(this);
    }

private:
    int width;
    int height;
    WidgetManager* widgets;
};

void editorEntry(void* arg) {
    UserEditor t(200, 200);

    struct gfx_event e;
    while (1){
        gfx_get_event(&e, GFX_EVENT_BLOCKING); /* alt: GFX_EVENT_NONBLOCKING */
        if(t.eventHandler(&e) == -1) break;
        t.draw();
    }
    return;
}

class Users : public Window {
public:
    Users(int width, int height) : Window(200, 200, "Users", 1) {
        this->width = width;
        this->height = height;

        /* Create widgets */
        widgets = new WidgetManager();
        widgets->addWidget(new Button(10, 10, 100, 12, "Button", []() {
            printf("Button pressed!\n");
        }));
        widgets->addWidget(new Button(10, 30, 100, 12, "Start Edit", []() {

            Thread* editor = new Thread(editorEntry, 0);
            editor->start(0);

        }));

        Input* input = new Input(10, 50, 100, 12, "Input");
        widgets->addWidget(input);
    
        widgets->addWidget(new Checkbox(10, 110, true));
        widgets->addWidget(new Label(30, 110, 100, 12, "Checkbox"));
        widgets->addWidget(new Checkbox(10, 130, false));
    }

    int eventHandler(struct gfx_event* event) {
        switch (event->event)
        {
        case GFX_EVENT_RESOLUTION:
            /* update screensize */
            break;
        case GFX_EVENT_EXIT:
            delete widgets;
            exit();
            /* exit */
            return -1;
        case GFX_EVENT_KEYBOARD:
            widgets->Keyboard(event->data);
            /* keyboard event in e.data */
            break;
        case GFX_EVENT_MOUSE:
            widgets->Mouse(event->data, event->data2);
            break;
        }
        return 0;
    }

    void draw() {
        /* Clear screen */
        drawRect(0, 0, width, height, 30);
        /* Draw widgets */
        widgets->draw(this);
    }

private:
    int width;
    int height;
    WidgetManager* widgets;
};

int main()
{
    Users t(200, 200);

    struct gfx_event e;
    while (1){
        gfx_get_event(&e, GFX_EVENT_BLOCKING); /* alt: GFX_EVENT_NONBLOCKING */
        if(t.eventHandler(&e) == -1) break;
        t.draw();
    }
    return 0;
}