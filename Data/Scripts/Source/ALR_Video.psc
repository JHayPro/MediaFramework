ScriptName ALR_Video
Import MediaLoadScreens

Event OnInit()
    Debug.Trace("[ALR_Video] OnInit()")
    Bool success = False

    MediaLoadScreensOptions options
    options.persistentPerInstance = false
    options.persistentCrossInstance = false

    String MediaFolderPath = "ALR_Video"
    Int MediaLoadscreensVersion = 0
    Int Priority = 0

    success = MediaLoadScreens.QueueMediaFileOrFolder(options, MediaFolderPath, MediaLoadscreensVersion, Priority)
    Debug.Trace("[ALR_Video] Queued FOLDER: " + MediaFolderPath)

    If (success)
        Debug.Trace("[ALR_Video] SUCCESS")
    Else
        Debug.Trace("[ALR_Video] FAILED")
    EndIf
EndEvent