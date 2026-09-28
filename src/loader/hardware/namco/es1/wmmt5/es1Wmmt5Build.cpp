#include "es1Wmmt5Build.hpp"

#include "es1Wmmt5.h"
#include "../es1Title.h"

namespace
{

/* One row per group, in the struct's order: log, HASP, network, steering,
 * card reader, card vendor, unit check. */

/* Export 5DX+ 2.20.02, the build axylol/fivedxp targets. */
constexpr Wmmt5Build ExportPlus = {
    "5DX+ export 2.20.02",
    '2',
    {0x080bc980, 0x080bca60, 0x080bcb40},
    0x0a982740, 0x0a9827e0, 0x0a9828cc, 0x0a9829b8, 0x0a9836d0, 0x0a983538, 0x0a983604,
    0x082519d0, 0x084ec560, 0x0aa75120, 0x0a393be0, 0x08401a70, 0x0aa77780, 0x08288040,
    0x082528d0, 0x084eb000, 0x0827f7e3, 0x0827f9fc, 0x0aafaa88,
    0x080dfd50, 0x080dfcc0,
    0x0aa62c34, 0x0aa62764, 0x080ead50, 0x0aa6200c, 0x0aa61de6, 0x0aa61eec, 0x0aa61a1a,
    0x0aa6236e, 0x0aa62116, 0x0aa62248,
    0x080eefc0, 0x080eef50,
    0x084fa120, 0x083c1800, 0x238, 0x448,
};

/* China 5DX+ 2.22.01. Mapped from the export build by masked byte patterns
 * and instruction alignment; the unit check's object grew by eight bytes. */
constexpr Wmmt5Build ChinaPlus = {
    "5DX+ China 2.22.01",
    '5',
    {0x080bc9f0, 0x080bcad0, 0x080bcbb0},
    0x0a9847f0, 0x0a984890, 0x0a98497c, 0x0a984a68, 0x0a985780, 0x0a9855e8, 0x0a9856b4,
    0x08251a40, 0x084ed1c0, 0x0aa771d0, 0x0a395ac0, 0x08401fc0, 0x0aa79830, 0x082880b0,
    0x08252940, 0x084ebc30, 0x0827f853, 0x0827fa6c, 0,
    0x080dfdc0, 0x080dfd30,
    0x0aa64ce4, 0x0aa64814, 0x080eadc0, 0x0aa640bc, 0x0aa63e96, 0x0aa63f9c, 0x0aa63aca,
    0x0aa6441e, 0x0aa641c6, 0x0aa642f8,
    0x080ef030, 0x080eefc0,
    0x084fb530, 0x083c23e0, 0x240, 0x450,
};

/* China 5DX 2.13.01, the earlier title; same systems, different layout. */
constexpr Wmmt5Build ChinaDx = {
    "5DX China 2.13.01",
    '5',
    {0x084d3190, 0x084d3270, 0x084d3350},
    0x0a308830, 0x0a3088d0, 0x0a3089bc, 0x0a308aa8, 0x0a3097c0, 0x0a309628, 0x0a3096f4,
    0x0824bb00, 0x0881a5d0, 0x0a3fb210, 0x08d133c0, 0x0a16b300, 0x0a3fd870, 0x08283d20,
    0x0824ca00, 0x08819070, 0x0827b4d3, 0x0827b6ec, 0,
    0x0848bb60, 0x0848bad0,
    0x0a3e8d24, 0x0a3e8854, 0x08495870, 0x0a3e80fc, 0x0a3e7ed6, 0x0a3e7fdc, 0x0a3e7b0a,
    0x0a3e845e, 0x0a3e8206, 0x0a3e8338,
    0x08499ae0, 0x08499a70,
    0x08825b20, 0x0809f860, 0x1e8, 0x3d8,
};

} // namespace

const Wmmt5Build *es1Wmmt5Build()
{
    if (es1TitleIs(ES1_TITLE_ID_WMMT5DXP))
        return &ExportPlus;
    if (es1TitleIs(ES1_TITLE_ID_WMMT5DXP_CHINA))
        return &ChinaPlus;
    if (es1TitleIs(ES1_TITLE_ID_WMMT5DX_CHINA))
        return &ChinaDx;
    return nullptr;
}
