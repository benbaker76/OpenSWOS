# Copy SWOS 2020's DLC competitions into OpenSWOS's classic folders, by group.
param([string]$From = "C:\Games\SWOS 2020\dlc\cup",
      [string]$To   = "$env:USERPROFILE\Documents\SWOS\assets\classic")

New-Item -ItemType Directory -Force "$To\uefa", "$To\worldcup", "$To\club" | Out-Null
Get-ChildItem $From -Recurse -Filter *.DIY | ForEach-Object {
    $where = $_.Directory.Name
    # Club cups first: "UEFA CHAMPIONS LEAGUE" and "UEFA EUROPEAN CUP" also say UEFA/EURO.
    $group = if ($where -match 'CHAMPIONS LEAGUE|EUROPEAN CUP|CUP WINNERS|UEFA CUP|EUROPA|CLUB') { 'club' }
             elseif ($where -match 'WORLD CUP') { 'worldcup' }
             elseif ($where -match 'EURO|UEFA') { 'uefa' }
             else { $null }
    if ($group) {
        Copy-Item $_.FullName "$To\$group\" -Force
        "{0,-9} {1,-14} <- {2}" -f $group, $_.Name, $where
    } else {
        "skipped   {0,-14} ({1}: not a World Cup, Euro or club cup)" -f $_.Name, $where
    }
}
