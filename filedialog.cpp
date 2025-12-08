
#include "filedialog.h"


namespace FileDialog
{
    nfdwindowhandle_t parentWindow;

    void init()
    {
        NFD_Init();
    }

    void destroy()
    {
        NFD_Quit();
    }

    std::string openDialog(const std::vector<nfdu8filteritem_t>& filters)
    {
        nfdu8char_t* outPath;
        nfdopendialogu8args_t args = { 0 };
        args.filterCount = 1;
        args.filterList = filters.data();
        args.parentWindow = parentWindow;

        nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
        if (result == NFD_OKAY)
        {
            std::string result = std::string(outPath);
            NFD_FreePathU8(outPath);
            return result;
        }
        else if (result == NFD_CANCEL)
        {
            return "";
        }
    }

    std::string saveDialog(const std::string& defaultFileName, const std::vector<nfdu8filteritem_t>& filters)
    {
        nfdu8char_t* outPath;
        nfdsavedialogu8args_t args = { 0 };
        args.defaultName = defaultFileName.c_str();
        args.filterCount = 1;
        args.filterList = filters.data();
        args.parentWindow = parentWindow;

        nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);
        if (result == NFD_OKAY)
        {
            std::string result = std::string(outPath);
            NFD_FreePathU8(outPath);
            return result;
        }
        else if (result == NFD_CANCEL)
        {
            return "";
        }
    }
}
