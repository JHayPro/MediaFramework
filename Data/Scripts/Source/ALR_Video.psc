ScriptName ALR_Video Extends Quest
Import MediaLoadscreens

String Property MediaFolderPath = "ALR_Video" Auto
Int Property Priority = 0 Auto
Bool Property Persistent = True Auto

Event OnInit()
    Debug.Trace("[ALR_Video] OnInit()")
    Bool success = False

    success = QueueMediaFolder(MediaFolderPath, Priority, Persistent)
    Debug.Trace("[ALR_Video] Queued FOLDER: " + MediaFolderPath)

    If (success)
        Debug.Trace("[ALR_Video] SUCCESS")
    Else
        Debug.Trace("[ALR_Video] FAILED")
    EndIf
EndEvent