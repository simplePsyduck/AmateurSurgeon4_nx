/* as4_setup_plan.c -- Amateur Surgeon 4's part of the first launch: its plan
 * for the runtime's dcr_setup.c, which does the work.
 *
 * The bar, in permille of the whole first launch:
 *   100- 500  libgame.so unpacked from the APK (by bytes)
 *   550- 950  the Java class list (by dex file)
 *        1000 the game starts
 *
 * .setup keys, which must not change (or every player unpacks again once):
 * libgame.so, classes.txt. MIT.
 */
#include "dcr_setup.h"

static const char *const k_libs[] = {"libgame.so"};

const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 1,
    .libs_what = "Unpacking the game",
    .apk_requirement = "This port needs the Android APK of Amateur Surgeon 4\n"
                       "(com.turner.amateursurgeon4, armeabi-v7a): use your own copy.",
    .libs_p0 = 100,
    .libs_p1 = 500,
    .classes_p0 = 550,
    .classes_p1 = 950,
};
