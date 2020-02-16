function supported_formats(printer)
{
    var formats = printer.attrs["document-format-supported"].value;
    var mimetypes = [];
    var supported = [];
     if(has(formats, "application/pdf"))
     {
         mimetypes.push("application/pdf");
         supported.push("PDF");
     }
     if(has(formats, "image/jpeg"))
     {
         mimetypes.push("image/jpeg");
         supported.push("JPEG");
     }

     if(supported.length == 0)
     {
         supported.push(qsTr("No compatible formats supported"))
     }


     //var info = "MFG:Hewlett-Packard;CMD:PJL,BIDI-ECP,PJL,POSTSCRIPT,PDF,PCLXL,PCL;MDL:HP LaserJet P3010 Series;CLS:PRINTER;DES:Hewlett-Packard ".split(";");
     var maybe = []
     var info = printer.attrs["printer-info"].value.split(";");
     for(var i in info)
     {
         if(info[i].split(":")[0] == "CMD")
         {
             if(has(info[i].split(":")[1].split(","), "PDF"))
             {
                 mimetypes.push("application/pdf");
                 maybe.push("PDF");
             }
             break;
         }
     }

     return {supported: supported.join(" "), maybe: maybe.join(" "), mimetypes: mimetypes};
}

function has(arrayish, what)
{
    for(var i in arrayish)
    {
        if(arrayish[i] == what)
            return true
    }
    return false
}

function ippName(name, value)
{
    switch(name) {
    case "job-state":
        switch(value) {
        case 3:
            return qsTr("pending");
        case 4:
            return qsTr("pending-held");
        case 5:
            return qsTr("processing");
        case 6:
            return qsTr("processing-stopped");
        case 7:
            return qsTr("canceled");
        case 8:
            return qsTr("aborted");
        case 9:
            return qsTr("completed");
        default:
            return qsTr("unknown state ")+value
        }
    case "print-quality":
        switch(value) {
        case 3:
            return qsTr("draft");
        case 4:
            return qsTr("normal");
        case 5:
            return qsTr("high");
        default:
            return qsTr("unknown quality ")+value
        }
    case "orientation-requested":
        switch(value) {
        case 3:
            return qsTr("portrait");
        case 4:
            return qsTr("landscape");
        case 5:
            return qsTr("reverse landscape");
        case 6:
            return qsTr("reverse portrait");
        default:
            return qsTr("unknown orientation ")+value
        }
    case "printer-resolution":
        var units = "";
        if(value.units==3) {
            units=qsTr("dpi");
        } else if (units==4){
            units=qsTr("dots/cm")
        }
        return ""+value.x+"x"+value.y+units;
    }
    return value;
}

function endsWith(ending, string)
{
    return string.lastIndexOf(ending) == (string.length - ending.length);
}
