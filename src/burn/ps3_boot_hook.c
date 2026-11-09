#include <sys/types.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    unsigned int hddFreeSizeKB;
    unsigned int sizeKB;
    unsigned int sysSizeKB;
} CellGameContentSize;

#ifdef __cplusplus
extern "C" {
#endif

extern int __real_cellGameBootCheck(unsigned int *type, unsigned int *attributes, CellGameContentSize *size, char *dirName);
extern int __real_cellGameContentPermit(char *content_info_path, char *usrDirPath);

int __wrap_cellGameBootCheck(unsigned int *type, unsigned int *attributes, CellGameContentSize *size, char *dirName)
{
    int ret = -1;
    if (__real_cellGameBootCheck) {
        ret = __real_cellGameBootCheck(type, attributes, size, dirName);
    }
    if (ret < 0 || !dirName || dirName[0] == '\0') {
        if (type) *type = 2; /* CELL_GAME_GAMETYPE_HDD */
        if (attributes) *attributes = 0;
        if (size) {
            size->hddFreeSizeKB = 10000000;
            size->sizeKB = 100000;
            size->sysSizeKB = 0;
        }
        if (dirName) strcpy(dirName, "SSNE10001");
        return 0;
    }
    return ret;
}

int __wrap_cellGameContentPermit(char *content_info_path, char *usrDirPath)
{
    int ret = -1;
    if (__real_cellGameContentPermit) {
        ret = __real_cellGameContentPermit(content_info_path, usrDirPath);
    }
    if (ret < 0 || !usrDirPath || usrDirPath[0] == '\0') {
        if (content_info_path) strcpy(content_info_path, "/dev_hdd0/game/SSNE10001");
        if (usrDirPath) strcpy(usrDirPath, "/dev_hdd0/game/SSNE10001/USRDIR");
        return 0;
    }
    return ret;
}

#ifdef __cplusplus
}
#endif
