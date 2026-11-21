#include "form_params.h"
#include <assert.h>

int main(void)
{
    char value[64];
    // Exact app/browser targeted commands must not trigger bulk mode handling.
    const char *targeted[] = {
        "control_outlet=2&outlet_mode=auto",
        "outlet_mode=manual&control_outlet=4",
    };
    for (size_t i = 0; i < sizeof(targeted) / sizeof(targeted[0]); i++) {
        assert(!get_param(targeted[i], "mode", value, sizeof(value)));
        assert(get_param(targeted[i], "outlet_mode", value, sizeof(value)));
        assert(get_param(targeted[i], "control_outlet", value, sizeof(value)));
    }
    assert(get_param("outlet_mode=auto&mode=manual", "mode", value, sizeof(value)));
    assert(!strcmp(value, "manual"));
    assert(get_param("mode=auto&control_outlet=2", "mode", value, sizeof(value)));
    assert(!strcmp(value, "auto"));
    assert(!get_param("dev_name=mode=manual&outlet_mode=auto", "mode", value, sizeof(value)));
    assert(!get_param("xreset=1&not_reboot=1", "reset", value, sizeof(value)));
    assert(!get_param("xreset=1&not_reboot=1", "reboot", value, sizeof(value)));
    assert(get_param("name=Grow+room%2B%26%3D&mode=auto", "name", value, sizeof(value)));
    assert(!strcmp(value, "Grow room+&="));
    assert(get_param("name=&mode=auto", "name", value, sizeof(value)) && !value[0]);
    assert(!get_param(NULL, "mode", value, sizeof(value)));
    assert(!get_param("mode=auto", "mode", value, 0));
    puts("Exact form parameter regression tests passed");
    return 0;
}
