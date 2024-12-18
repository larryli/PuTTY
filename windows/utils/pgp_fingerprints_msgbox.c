/*
 * Display the fingerprints of the PGP Master Keys to the user as a
 * GUI message box.
 */

#include "putty.h"

void pgp_fingerprints_msgbox(HWND owner)
{
    message_box(
        owner,
        "这些是 PuTTY PGP 主密钥指纹。\n"
        "它们可用于建立从此可执行文件到另一个可执行文件的信任路径。\n"
        "有关详细信息请参阅手册。\n"
        "(注意：这些指纹与 SSH 无关！)\n"
        "\n"
        "PuTTY 主密钥 " PGP_MASTER_KEY_YEAR
        " (" PGP_MASTER_KEY_DETAILS "):\n"
        "  " PGP_MASTER_KEY_FP "\n\n"
        "上级主密钥 (" PGP_PREV_MASTER_KEY_YEAR
        ", " PGP_PREV_MASTER_KEY_DETAILS "):\n"
        "  " PGP_PREV_MASTER_KEY_FP,
        "PGP 指纹", MB_ICONINFORMATION | MB_OK,
        false, HELPCTXID(pgp_fingerprints));
}
