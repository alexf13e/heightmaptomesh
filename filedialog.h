
#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

#include <string>
#include <vector>

#include <nfd.h>

namespace FileDialog
{
    extern nfdwindowhandle_t parentWindow;

    void init();
    void destroy();
    std::string openDialog(const std::vector<nfdu8filteritem_t>& filters);
    std::string saveDialog(const std::string& defaultFileName, const std::vector<nfdu8filteritem_t>& filters);
}



#endif // !FILE_DIALOG_H
