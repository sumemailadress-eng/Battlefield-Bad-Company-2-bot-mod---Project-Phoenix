[CmdletBinding()]
param([string]$LearnFile, [int]$Line = 0)
$ErrorActionPreference = 'Stop'
if (!$LearnFile) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object Windows.Forms.OpenFileDialog
    $dialog.Title = 'Open a Phoenix map learning file after stopping the server'
    $dialog.Filter = 'Phoenix map learning (*.learn)|*.learn'
    $dialog.InitialDirectory = Join-Path $PSScriptRoot 'navcache'
    if ($dialog.ShowDialog() -ne 'OK') { return }
    $LearnFile = $dialog.FileName
}
$file = Get-Item -LiteralPath $LearnFile
if ($file.Extension -ne '.learn') { throw 'Choose a .learn file' }
# Latin-1 round-trips every byte, including older Phoenix map-name encodings.
$encoding = [Text.Encoding]::GetEncoding(28591)
$original = $encoding.GetString([IO.File]::ReadAllBytes($file.FullName))
$rows = [regex]::Matches($original,'[^\r\n]+(?:\r\n|\n|\r|$)|\r\n|\n|\r')
$records = for ($i=0; $i -lt $rows.Count; $i++) {
    $record=$rows[$i].Value.TrimEnd([char[]]"`r`n")
    if ($record -match '^(stuck|spot|death|kill|walk|drive|water|vstuck)\s') {
        [pscustomobject]@{Line=$i+1; Type=$Matches[1]; Record=$record}
    }
}
if (!$Line) {
    $selected=$records | Out-GridView -Title 'Select one learned entry to remove' -OutputMode Single
    if (!$selected) {return}
    $Line=$selected.Line
}
if (!($records | Where-Object Line -eq $Line)) {throw 'Select a listed learned entry'}
$row=$rows[$Line-1]
$tmp=Join-Path $file.DirectoryName ([Guid]::NewGuid().ToString()+'.tmp')
$backup=$file.FullName+'.'+[Guid]::NewGuid().ToString()+'.bak'
try {
    [IO.File]::WriteAllBytes($tmp,$encoding.GetBytes($original.Remove($row.Index,$row.Length)))
    if ($encoding.GetString([IO.File]::ReadAllBytes($file.FullName)) -cne $original) {throw 'Map learning changed. Reopen the file after stopping the server.'}
    [IO.File]::Replace($tmp,$file.FullName,$backup)
    Write-Output "Removed line $Line. Backup saved to $backup"
} finally {if(Test-Path -LiteralPath $tmp){Remove-Item -LiteralPath $tmp}}
