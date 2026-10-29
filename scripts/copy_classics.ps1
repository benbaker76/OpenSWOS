# Copy SWOS 2020's DLC competitions into OpenSWOS's classic folders, by group.
param([string]$From = "C:\Games\SWOS 2020\dlc\cup",
      [string]$To   = "$env:USERPROFILE\Documents\SWOS\assets\classic")

New-Item -ItemType Directory -Force "$To\uefa", "$To\worldcup" | Out-Null
Get-ChildItem $From -Recurse -Filter *.DIY | ForEach-Object {
    $where = $_.Directory.Name
    $group = if ($where -match 'WORLD CUP') { 'worldcup' }
             elseif ($where -match 'EURO|UEFA') { 'uefa' }
             else { $null }
    if ($group) {
        Copy-Item $_.FullName "$To\$group\" -Force
        "{0,-9} {1,-14} <- {2}" -f $group, $_.Name, $where
    } else {
        "skipped   {0,-14} ({1}: neither a World Cup nor a Euro)" -f $_.Name, $where
    }
}
