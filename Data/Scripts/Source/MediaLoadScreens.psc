ScriptName MediaLoadScreens Native Hidden

;cgf "MediaLoadScreens.QueueMediaFileOrFolder" None "ALR_Video" 1 0
;cgf "MediaLoadScreens.test"

Struct MediaLoadScreensOptions
    bool persistentPerInstance = false
    bool persistentCrossInstance = false
EndStruct

bool Function QueueMediaFileOrFolder(MediaLoadScreensOptions options, string path, int version = 0, int priority = 0) global native
Function test() global native