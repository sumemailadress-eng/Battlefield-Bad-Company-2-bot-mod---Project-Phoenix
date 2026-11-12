# Project Phoenix Launcher: one window for the whole Bad Company 2 bot setup, in BC2's colours (olive, sand and amber).
#   - starts the backend: MySQL, the web server (if BF-Portable has one), the redirector and the BC2 emulator
#   - Servers 1 - 4 (BF-Portable's BFBC2.bat ... BFBC2_4.bat): start / stop each, bots on or off per server (Phoenix is
#     attached to the server process once its level is up), and each server's maplist.txt (BC2: one mode per list)
#   - the bot settings in phoenix.ini (shared by all servers, read when the bots attach)
#   - tools: the 3D map and route editor (tools\Map3D, needs Node.js), LearnViewer, the bot log, PLAY (starts BC2)
# Run Phoenix-Launcher.bat. PHOENIX_LAUNCHER_TEST=1: reads everything and prints what it found, starts nothing.
# Bad Company 2 is Frostbite 1.5: its own server exe (Frost.Game.Main_Win32_Final.exe), its own RCON commands
# (admin.runNextLevel, admin.restartMap, mapList.nextLevelIndex - not the BF3 / BF4 / Hardline ones) and its own
# map list format (a mode line, then "Levels/<map> <rounds>").
param([string]$Portable = '')
$ErrorActionPreference = 'Continue'
$Version = '1.7.2'

# ---------------------------------------------------------------- launcher settings
$setPath = Join-Path $PSScriptRoot 'Phoenix-Launcher.ini'
$set = @{ Selected = '1'; Portable = ''; ServerDir = ''; Game = ''; BotsOn1 = '1'; BotsOn2 = '1'; BotsOn3 = '1'; BotsOn4 = '1'; Rounds = '1' }
if (Test-Path $setPath) { foreach ($l in Get-Content $setPath) { if ($l -match '^\s*([A-Za-z0-9]+)\s*=\s*(.*?)\s*$') { $set[$Matches[1]] = $Matches[2] } } }
function Save-Settings {
    $lines = @('; Project Phoenix Launcher settings (the launcher writes this; paths can be edited by hand)')
    foreach ($k in ($set.Keys | Sort-Object)) { $lines += "$k=$($set[$k])" }
    try { Set-Content $setPath $lines -Encoding ascii } catch {}
}
# the old launcher's launcher.ini (PhoenixLauncher.exe) already knows where the server and the game are
$oldIni = Join-Path $PSScriptRoot 'launcher.ini'
$old = @{}
if (Test-Path $oldIni) { foreach ($l in Get-Content $oldIni) { if ($l -match '^\s*([A-Za-z0-9]+)\s*=\s*(.*?)\s*$') { $old[$Matches[1]] = $Matches[2] } } }

if (-not $Portable) { $Portable = $set.Portable }
if (-not $Portable -and $old.ServerPath) { $Portable = Split-Path (Split-Path $old.ServerPath) }
if (-not $Portable) { foreach ($d in 'C:', 'D:', 'E:', 'F:') { if (Test-Path "$d\BF-Portable\Battlefield_bc2_Server") { $Portable = "$d\BF-Portable"; break } } }
if (-not $Portable) { $Portable = 'D:\BF-Portable' }
$server = $set.ServerDir
if (-not $server -and $old.ServerPath) { $server = Split-Path $old.ServerPath }
if (-not $server) { $server = Join-Path $Portable 'Battlefield_bc2_Server' }
$gameExe = $set.Game
if (-not $gameExe -and $old.GamePath) { $gameExe = $old.GamePath }
$xampp = Join-Path $Portable 'xampp'
$redirDir = Join-Path $Portable 'Redirector'
$emuDir = Join-Path $Portable 'BFBC2-EMU'
$reportsDir = Join-Path $PSScriptRoot 'reports'
$attachExe = Join-Path $PSScriptRoot 'bin\PhoenixAttach.exe'
$iniPath = Join-Path $PSScriptRoot 'phoenix.ini'
$map3dDir = Join-Path $PSScriptRoot 'tools\Map3D'; $map3dPort = 8644
$SERVER_EXE = 'Frost.Game.Main_Win32_Final'

# ---------------------------------------------------------------- maps and modes (BC2 level names)
# code, name, mode, vietnam
$mapDefs = @(
    @('MP_001', 'Panama Canal', 'CONQUEST', 0), @('MP_003', 'Laguna Alta', 'CONQUEST', 0), @('MP_005', 'Atacama Desert', 'CONQUEST', 0),
    @('MP_006CQ', 'Arica Harbor', 'CONQUEST', 0), @('MP_007', 'White Pass', 'CONQUEST', 0), @('MP_008CQ', 'Nelson Bay', 'CONQUEST', 0),
    @('MP_009CQ', 'Laguna Presa', 'CONQUEST', 0), @('MP_012CQ', 'Port Valdez', 'CONQUEST', 0), @('BC1_Oasis_CQ', 'Oasis', 'CONQUEST', 0),
    @('BC1_Harvest_Day_CQ', 'Harvest Day', 'CONQUEST', 0), @('MP_SP_005CQ', 'Heavy Metal', 'CONQUEST', 0),
    @('MP_002', 'Valparaiso', 'RUSH', 0), @('MP_004', 'Isla Inocentes', 'RUSH', 0), @('MP_006', 'Arica Harbor', 'RUSH', 0),
    @('MP_008', 'Nelson Bay', 'RUSH', 0), @('MP_005GR', 'Atacama Desert', 'RUSH', 0), @('MP_007GR', 'White Pass', 'RUSH', 0),
    @('MP_009GR', 'Laguna Presa', 'RUSH', 0), @('MP_012GR', 'Port Valdez', 'RUSH', 0), @('BC1_Harvest_Day_GR', 'Harvest Day', 'RUSH', 0),
    @('BC1_Oasis_GR', 'Oasis', 'RUSH', 0), @('MP_SP_002GR', 'Cold War', 'RUSH', 0),
    @('MP_001SR', 'Panama Canal', 'SQRUSH', 0), @('MP_002SR', 'Valparaiso', 'SQRUSH', 0), @('MP_003SR', 'Laguna Alta', 'SQRUSH', 0),
    @('MP_005SR', 'Atacama Desert', 'SQRUSH', 0), @('MP_009SR', 'Laguna Presa', 'SQRUSH', 0), @('MP_012SR', 'Port Valdez', 'SQRUSH', 0),
    @('BC1_Harvest_Day_SR', 'Harvest Day', 'SQRUSH', 0), @('BC1_Oasis_SR', 'Oasis', 'SQRUSH', 0), @('MP_SP_002SR', 'Cold War', 'SQRUSH', 0),
    @('MP_001SDM', 'Panama Canal', 'SQDM', 0), @('MP_004SDM', 'Isla Inocentes', 'SQDM', 0), @('MP_006SDM', 'Arica Harbor', 'SQDM', 0),
    @('MP_007SDM', 'White Pass', 'SQDM', 0), @('MP_008SDM', 'Nelson Bay', 'SQDM', 0), @('MP_009SDM', 'Laguna Presa', 'SQDM', 0),
    @('BC1_Harvest_Day_SDM', 'Harvest Day', 'SQDM', 0), @('BC1_Oasis_SDM', 'Oasis', 'SQDM', 0), @('MP_SP_002SDM', 'Cold War', 'SQDM', 0),
    @('MP_SP_005SDM', 'Heavy Metal', 'SQDM', 0),
    @('NAM_MP_002CQ', 'Vantage Point', 'CONQUEST', 1), @('NAM_MP_002R', 'Vantage Point', 'RUSH', 1), @('NAM_MP_002SR', 'Vantage Point', 'SQRUSH', 1), @('NAM_MP_002SDM', 'Vantage Point', 'SQDM', 1),
    @('NAM_MP_003CQ', 'Hill 137', 'CONQUEST', 1), @('NAM_MP_003R', 'Hill 137', 'RUSH', 1), @('NAM_MP_003SR', 'Hill 137', 'SQRUSH', 1), @('NAM_MP_003SDM', 'Hill 137', 'SQDM', 1),
    @('NAM_MP_005CQ', 'Cao Son Temple', 'CONQUEST', 1), @('NAM_MP_005R', 'Cao Son Temple', 'RUSH', 1), @('NAM_MP_005SR', 'Cao Son Temple', 'SQRUSH', 1), @('NAM_MP_005SDM', 'Cao Son Temple', 'SQDM', 1),
    @('NAM_MP_006CQ', 'Phu Bai Valley', 'CONQUEST', 1), @('NAM_MP_006R', 'Phu Bai Valley', 'RUSH', 1), @('NAM_MP_006SR', 'Phu Bai Valley', 'SQRUSH', 1), @('NAM_MP_006SDM', 'Phu Bai Valley', 'SQDM', 1),
    @('NAM_MP_007CQ', 'Operation Hastings', 'CONQUEST', 1), @('NAM_MP_007R', 'Operation Hastings', 'RUSH', 1), @('NAM_MP_007SR', 'Operation Hastings', 'SQRUSH', 1), @('NAM_MP_007SDM', 'Operation Hastings', 'SQDM', 1)
)
# mode key, name, bots play the objectives (1) or just fight (0)
$modes = @(@('CONQUEST', 'Conquest', 1), @('RUSH', 'Rush', 1), @('SQRUSH', 'Squad Rush', 1), @('SQDM', 'Squad Deathmatch', 0))
function Level-Code($lv) { (([string]$lv) -replace '^(?i)levels/', '').Trim() }
function Map-Def($lv) { $c = Level-Code $lv; foreach ($m in $mapDefs) { if ($m[0] -ieq $c) { return $m } }; $null }
function Map-Name($lv) { $d = Map-Def $lv; if ($d) { $d[1] } else { Level-Code $lv } }
function Mode-Name($code) { foreach ($m in $modes) { if ($m[0] -ieq $code) { return $m[1] } }; $code }

# ---------------------------------------------------------------- servers 1 - 4 (read from BF-Portable's own start files)
function Read-Bat($path) {
    $h = @{}
    if (-not (Test-Path $path)) { return $h }
    $t = Get-Content $path -Raw
    if ($t -match '-serverInstancePath\s+"?([^"\s]+)') { $h.INST = $Matches[1] -replace '/', '\' }
    if ($t -match '(?<![A-Za-z])-port\s+(\d+)') { $h.PORT = [int]$Matches[1] }
    if ($t -match '-Login\s+(\S+)') { $h.LOGIN = $Matches[1] }
    return $h
}
function Read-Options($path) {
    $h = @{}
    if (Test-Path $path) { foreach ($l in Get-Content $path) { if ($l -match '^\s*([A-Za-z]+)\s*=\s*(.*?)\s*$') { $h[$Matches[1]] = $Matches[2] } } }
    return $h
}
$srv = @{}
function Load-Servers {
    foreach ($n in 1..4) {
        $bat = Join-Path $server $(if ($n -eq 1) { 'BFBC2.bat' } else { "BFBC2_$n.bat" })
        $b = Read-Bat $bat
        $instRel = if ($b.INST) { $b.INST.TrimEnd('\') } elseif ($n -eq 1) { 'Instance' } else { "Instance$n" }
        $inst = Join-Path $server $instRel
        $o = Read-Options (Join-Path $inst 'ServerOptions.ini')
        $srv[$n] = @{
            N = $n; Bat = $bat; Inst = $inst
            Name = $(if ($o.Name) { $o.Name } else { "BC2 Server $n" })
            GamePort = $(if ($b.PORT) { $b.PORT } elseif ($o.Port) { [int]$o.Port } else { 19566 + $n })
            RconPort = $(if ($o.RemoteAdminPort) { [int]$o.RemoteAdminPort } else { 47399 + $n })
            Pass = $o.RemoteAdminPassword; Login = $(if ($b.LOGIN) { $b.LOGIN } else { "bfbc2_$n" })
            MapList = Join-Path $inst 'maplist.txt'; Options = Join-Path $inst 'ServerOptions.ini'
            State = 'off'; Info = $null; Pid = 0; Bots = 'off'
        }
    }
}
Load-Servers

# ---------------------------------------------------------------- RCON (BC2's Frostbite RCON: the same packets as BF3's)
function Send-Packet($stream, [int]$seq, [string[]]$words) {
    $body = New-Object IO.MemoryStream
    foreach ($w in $words) {
        $b = [Text.Encoding]::ASCII.GetBytes($w)
        $body.Write([BitConverter]::GetBytes([int]$b.Length), 0, 4); $body.Write($b, 0, $b.Length); $body.WriteByte(0)
    }
    $wb = $body.ToArray()
    $hdr = [BitConverter]::GetBytes([int]($seq -band 0x3fffffff)) + [BitConverter]::GetBytes([int](12 + $wb.Length)) + [BitConverter]::GetBytes([int]$words.Count)
    $stream.Write($hdr + $wb, 0, 12 + $wb.Length)
}
function Read-Exact($stream, [int]$n) {
    $buf = New-Object byte[] $n; $got = 0
    while ($got -lt $n) { $r = $stream.Read($buf, $got, $n - $got); if ($r -le 0) { throw 'connection closed' }; $got += $r }
    return $buf
}
function Read-Packet($stream) {
    while ($true) {
        $h = Read-Exact $stream 12
        $seq = [BitConverter]::ToInt32($h, 0); $size = [BitConverter]::ToInt32($h, 4); $count = [BitConverter]::ToInt32($h, 8)
        $b = Read-Exact $stream ($size - 12); $o = 0; $words = @()
        for ($i = 0; $i -lt $count; $i++) { $len = [BitConverter]::ToInt32($b, $o); $words += [Text.Encoding]::ASCII.GetString($b, $o + 4, $len); $o += 5 + $len }
        if ($seq -band 0x80000000) { continue }   # (a server event, not our answer)
        return , $words
    }
}
# one word array per command, or $null when that server doesn't answer. BC2: serverInfo needs no login, the rest do.
function Invoke-Rcon($s, [object[]]$commands) {
    try {
        $c = New-Object Net.Sockets.TcpClient
        $iar = $c.BeginConnect('127.0.0.1', $s.RconPort, $null, $null)
        if (-not $iar.AsyncWaitHandle.WaitOne(600)) { $c.Close(); return $null }
        $c.EndConnect($iar); $st = $c.GetStream(); $st.ReadTimeout = 4000; $seq = 1
        $needLogin = @($commands | Where-Object { $_[0] -ne 'serverInfo' }).Count -gt 0
        if ($needLogin -and $s.Pass) { Send-Packet $st $seq @('login.plainText', $s.Pass); $seq++; Read-Packet $st | Out-Null }
        $out = @()
        foreach ($cmd in $commands) { Send-Packet $st $seq ([string[]]$cmd); $seq++; $out += , (Read-Packet $st) }
        $c.Close()
        return , $out
    } catch { return $null }
}

# ---------------------------------------------------------------- processes
function Get-ServerProcs {
    if (-not (Get-Process $SERVER_EXE -ErrorAction SilentlyContinue)) { return @() }
    return @(Get-CimInstance Win32_Process -Filter "Name='$SERVER_EXE.exe'" -ErrorAction SilentlyContinue)
}
function Find-ServerPid($s, $procs) {
    foreach ($p in $procs) { if ($p.CommandLine -and $p.CommandLine -match "(?<![A-Za-z])-port\s+$($s.GamePort)(\D|$)") { return [int]$p.ProcessId } }
    # a server running as administrator hides its command line from a launcher that isn't: find it by its game port instead
    try {
        $u = Get-NetUDPEndpoint -LocalPort $s.GamePort -ErrorAction Stop | Select-Object -First 1
        if ($u -and ($procs | Where-Object { [int]$_.ProcessId -eq [int]$u.OwningProcess })) { return [int]$u.OwningProcess }
    } catch {}
    if ($s.N -eq 1 -and $procs.Count -eq 1 -and -not $procs[0].CommandLine) { return [int]$procs[0].ProcessId }
    return 0
}
function Port-Open([int]$port) {
    try { $c = New-Object Net.Sockets.TcpClient; $iar = $c.BeginConnect('127.0.0.1', $port, $null, $null); $ok = $iar.AsyncWaitHandle.WaitOne(300) -and $c.Connected; $c.Close(); return $ok } catch { return $false }
}
$services = [ordered]@{
    mysql = @{ Name = 'MYSQL'; Proc = 'mysqld' }
    web   = @{ Name = 'WEB SERVER'; Proc = 'httpd' }
    redir = @{ Name = 'REDIRECTOR'; Proc = 'gosredirector.ea.com' }
    emu   = @{ Name = 'BC2 EMULATOR'; Proc = 'mase_bc2' }
}
function Svc-Running($key) { [bool](Get-Process $services[$key].Proc -ErrorAction SilentlyContinue) }
function Has-Web { Test-Path (Join-Path $xampp 'apache\bin\httpd.exe') }

# ---------------------------------------------------------------- phoenix.ini (shared by all servers)
function Ini-Get([string]$key, [string]$def) {
    if (Test-Path $iniPath) { foreach ($l in Get-Content $iniPath) { if ($l -match "^\s*$key\s*=\s*([^;]*)") { return $Matches[1].Trim() } } }
    return $def
}
function Ini-Set([string]$key, [string]$val, [string]$section = 'Behaviour') {
    $lines = [Collections.Generic.List[string]]@(if (Test-Path $iniPath) { Get-Content $iniPath } else { '; Project Phoenix BC2 - bot settings' })
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match "^\s*$key\s*=") { $lines[$i] = "$key=$val"; Set-Content $iniPath $lines -Encoding ascii; return } }
    $at = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match "^\s*\[$section\]") { $at = $i + 1; while ($at -lt $lines.Count -and $lines[$at] -notmatch '^\s*\[') { $at++ }; break } }
    if ($at -lt 0) { $lines.Add(''); $lines.Add("[$section]"); $lines.Add("$key=$val") } else { $lines.Insert($at, "$key=$val") }
    Set-Content $iniPath $lines -Encoding ascii
}

# the navcache (1.6.9+): one folder shared by every Phoenix install - NavcacheDir= in phoenix.ini, else next to the server
function Get-NavDir {
    $d = Ini-Get 'NavcacheDir' ''
    if ($d) { return $d }
    $s = Join-Path $server 'PhoenixNavcache'
    if ((Test-Path $s) -or -not (Test-Path (Join-Path $PSScriptRoot 'navcache'))) { return $s }
    return (Join-Path $PSScriptRoot 'navcache')
}

# ---------------------------------------------------------------- test mode
if ($env:PHOENIX_LAUNCHER_TEST -eq '1') {
    Write-Host "Project Phoenix Launcher $Version (test mode - nothing is started)"
    Write-Host "BF-Portable: $Portable   server folder: $server   game: $gameExe"
    foreach ($n in 1..4) { $s = $srv[$n]; Write-Host ("Server {0}: {1}  bat {2} ({3})  game port {4}  RCON {5}  list {6} ({7})" -f $n, $s.Name, (Split-Path $s.Bat -Leaf), (Test-Path $s.Bat), $s.GamePort, $s.RconPort, $s.MapList, (Test-Path $s.MapList)) }
    foreach ($k in $services.Keys) { Write-Host ("{0}: {1}" -f $services[$k].Name, $(if (Svc-Running $k) { 'running' } else { 'off' })) }
    Write-Host "phoenix.ini: BotsPerTeam=$(Ini-Get 'BotsPerTeam' '15') Difficulty=$(Ini-Get 'Difficulty' '2') Vehicles=$(Ini-Get 'Vehicles' '2') Routes=$(Ini-Get 'Routes' '1') AutoRoutes=$(Ini-Get 'AutoRoutes' '1')"
    Write-Host "navcache: $(Get-NavDir)"
    Write-Host "PhoenixAttach: $(Test-Path $attachExe)   3D map: $(Test-Path (Join-Path $map3dDir 'server.js'))   node: $([bool](Get-Command node -ErrorAction SilentlyContinue))"
    return
}

# ================================================================ window
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase

$cardXaml = ''
foreach ($n in 1..4) {
    $cardXaml += @"
<Button x:Name="Card$n" Grid.Column="$($n - 1)" Style="{StaticResource Card}" Tag="$n" Margin="0,0,$(if ($n -lt 4) { '10' } else { '0' }),0">
  <Grid>
   <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
   <Border Grid.Column="1" Width="78" Height="78" Margin="8,22,0,0" VerticalAlignment="Top" BorderBrush="#3A3D27" BorderThickness="1" x:Name="CardPicBox$n" Visibility="Collapsed">
     <Image x:Name="CardPic$n" Stretch="UniformToFill"/>
   </Border>
   <Grid>
    <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
    <DockPanel>
      <TextBlock DockPanel.Dock="Right" x:Name="CardState$n" Text="OFFLINE" FontFamily="Consolas" FontSize="12" Foreground="{StaticResource Dim}" VerticalAlignment="Center"/>
      <TextBlock Text="SERVER $n" FontFamily="{StaticResource Stencil}" FontSize="22" Foreground="{StaticResource Txt}"/>
    </DockPanel>
    <TextBlock Grid.Row="1" x:Name="CardName$n" Text="" FontSize="11" Foreground="{StaticResource Dim}" TextTrimming="CharacterEllipsis"/>
    <TextBlock Grid.Row="2" x:Name="CardMap$n" Text="-" Margin="0,6,0,0" FontSize="13" Foreground="{StaticResource Txt}" TextTrimming="CharacterEllipsis"/>
    <TextBlock Grid.Row="3" x:Name="CardPlayers$n" Text="" FontFamily="Consolas" FontSize="12" Foreground="{StaticResource Olive}"/>
    <TextBlock Grid.Row="4" x:Name="CardBots$n" Text="" Margin="0,4,0,0" FontFamily="Consolas" FontSize="12" Foreground="{StaticResource Amber}" TextTrimming="CharacterEllipsis"/>
   </Grid>
  </Grid>
</Button>
"@
}

$xamlText = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Project Phoenix Launcher - Battlefield Bad Company 2 bots" Width="1240" Height="900" MinWidth="1080" MinHeight="780"
        WindowStartupLocation="CenterScreen" Background="#0D0E09" FontFamily="Bahnschrift, Segoe UI" Foreground="#ECE4CA">
  <Window.Resources>
    <SolidColorBrush x:Key="Amber" Color="#E8A33D"/>
    <SolidColorBrush x:Key="Olive" Color="#9CAF55"/>
    <SolidColorBrush x:Key="Txt" Color="#ECE4CA"/>
    <SolidColorBrush x:Key="Dim" Color="#968F75"/>
    <SolidColorBrush x:Key="Panel" Color="#17190F"/>
    <SolidColorBrush x:Key="Edge" Color="#3A3D27"/>
    <FontFamily x:Key="Stencil">Stencil, Bahnschrift SemiBold Condensed, Impact</FontFamily>
    <FontFamily x:Key="Head">Bahnschrift SemiBold Condensed, Impact, Segoe UI</FontFamily>
    <LinearGradientBrush x:Key="Hazard" MappingMode="Absolute" SpreadMethod="Repeat" StartPoint="0,0" EndPoint="12,12">
      <GradientStop Color="#E8A33D" Offset="0"/><GradientStop Color="#E8A33D" Offset="0.5"/><GradientStop Color="#14150D" Offset="0.5"/><GradientStop Color="#14150D" Offset="1"/>
    </LinearGradientBrush>

    <Style x:Key="Btn" TargetType="Button">
      <Setter Property="Background" Value="#1E2114"/>
      <Setter Property="Foreground" Value="#ECE4CA"/>
      <Setter Property="BorderBrush" Value="#6E7A3A"/>
      <Setter Property="BorderThickness" Value="1,1,1,3"/>
      <Setter Property="FontFamily" Value="Bahnschrift SemiBold Condensed, Segoe UI"/>
      <Setter Property="FontSize" Value="15"/>
      <Setter Property="Padding" Value="12,6"/>
      <Setter Property="Margin" Value="0,0,8,8"/>
      <Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="Button">
            <Border Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="{TemplateBinding BorderThickness}" Padding="{TemplateBinding Padding}">
              <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
            </Border>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
      <Style.Triggers>
        <Trigger Property="IsMouseOver" Value="True"><Setter Property="Background" Value="#9CAF55"/><Setter Property="Foreground" Value="#0D0E09"/></Trigger>
        <Trigger Property="IsPressed" Value="True"><Setter Property="Background" Value="#E8A33D"/><Setter Property="Foreground" Value="#0D0E09"/></Trigger>
        <Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value="0.35"/></Trigger>
      </Style.Triggers>
    </Style>
    <Style x:Key="ABtn" TargetType="Button" BasedOn="{StaticResource Btn}">
      <Setter Property="BorderBrush" Value="#E8A33D"/>
      <Setter Property="Foreground" Value="#FFE2B0"/>
      <Setter Property="Background" Value="#2A1E0B"/>
      <Style.Triggers>
        <Trigger Property="IsMouseOver" Value="True"><Setter Property="Background" Value="#E8A33D"/><Setter Property="Foreground" Value="#0D0E09"/></Trigger>
        <Trigger Property="IsPressed" Value="True"><Setter Property="Background" Value="#ECE4CA"/><Setter Property="Foreground" Value="#0D0E09"/></Trigger>
        <Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value="0.35"/></Trigger>
      </Style.Triggers>
    </Style>
    <Style x:Key="Card" TargetType="Button">
      <Setter Property="Background" Value="#17190F"/>
      <Setter Property="BorderBrush" Value="#3A3D27"/>
      <Setter Property="BorderThickness" Value="1,1,1,3"/>
      <Setter Property="Foreground" Value="#ECE4CA"/>
      <Setter Property="Padding" Value="14,10"/>
      <Setter Property="Cursor" Value="Hand"/>
      <Setter Property="HorizontalContentAlignment" Value="Stretch"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="Button">
            <Grid>
              <Border Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="{TemplateBinding BorderThickness}" Padding="{TemplateBinding Padding}">
                <ContentPresenter HorizontalAlignment="Stretch"/>
              </Border>
              <Border x:Name="hover" Background="#ECE4CA" Opacity="0" IsHitTestVisible="False"/>
            </Grid>
            <ControlTemplate.Triggers>
              <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="hover" Property="Opacity" Value="0.06"/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
    <Style TargetType="ListBox">
      <Setter Property="Background" Value="#11120C"/>
      <Setter Property="Foreground" Value="#ECE4CA"/>
      <Setter Property="BorderBrush" Value="#3A3D27"/>
      <Setter Property="BorderThickness" Value="1"/>
      <Setter Property="ScrollViewer.HorizontalScrollBarVisibility" Value="Disabled"/>
    </Style>
    <Style TargetType="ListBoxItem">
      <Setter Property="Foreground" Value="#ECE4CA"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="ListBoxItem">
            <Border x:Name="b" Background="Transparent" BorderBrush="Transparent" BorderThickness="3,0,0,0" Padding="8,4">
              <ContentPresenter/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="b" Property="Background" Value="#22251A"/></Trigger>
              <Trigger Property="IsSelected" Value="True"><Setter TargetName="b" Property="Background" Value="#2E2A18"/><Setter TargetName="b" Property="BorderBrush" Value="#E8A33D"/><Setter Property="Foreground" Value="#FFF4D8"/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
    <Style x:Key="Chip" TargetType="ListBoxItem">
      <Setter Property="Foreground" Value="#ECE4CA"/>
      <Setter Property="Margin" Value="0,0,6,6"/>
      <Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="ListBoxItem">
            <Border x:Name="b" Background="#1E2114" BorderBrush="#3A3D27" BorderThickness="1" Padding="9,4">
              <ContentPresenter/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="b" Property="BorderBrush" Value="#9CAF55"/></Trigger>
              <Trigger Property="IsSelected" Value="True"><Setter TargetName="b" Property="Background" Value="#E8A33D"/><Setter TargetName="b" Property="BorderBrush" Value="#E8A33D"/><Setter Property="Foreground" Value="#0D0E09"/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
    <Style TargetType="ScrollBar">
      <Setter Property="Width" Value="8"/>
      <Setter Property="MinWidth" Value="8"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="ScrollBar">
            <Grid Background="#11120C">
              <Track x:Name="PART_Track" IsDirectionReversed="True">
                <Track.Thumb>
                  <Thumb>
                    <Thumb.Template>
                      <ControlTemplate TargetType="Thumb"><Border Background="#6E7A3A"/></ControlTemplate>
                    </Thumb.Template>
                  </Thumb>
                </Track.Thumb>
              </Track>
            </Grid>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
    <Style x:Key="H" TargetType="TextBlock">
      <Setter Property="FontFamily" Value="{StaticResource Stencil}"/>
      <Setter Property="FontSize" Value="17"/>
      <Setter Property="Foreground" Value="#E8A33D"/>
      <Setter Property="Margin" Value="0,0,0,8"/>
    </Style>
    <Style x:Key="Lbl" TargetType="TextBlock">
      <Setter Property="FontFamily" Value="Consolas"/>
      <Setter Property="FontSize" Value="11"/>
      <Setter Property="Foreground" Value="#968F75"/>
      <Setter Property="Margin" Value="0,2,0,4"/>
    </Style>
  </Window.Resources>

  <Grid>
    <!-- desert haze from the top and a faint olive floor -->
    <Rectangle IsHitTestVisible="False" VerticalAlignment="Top" Height="300">
      <Rectangle.Fill>
        <RadialGradientBrush Center="0.82,0" GradientOrigin="0.82,0" RadiusX="0.55" RadiusY="1">
          <GradientStop Color="#40E8A33D" Offset="0"/><GradientStop Color="#00E8A33D" Offset="1"/>
        </RadialGradientBrush>
      </Rectangle.Fill>
    </Rectangle>
    <Rectangle IsHitTestVisible="False" VerticalAlignment="Bottom" Height="260">
      <Rectangle.Fill>
        <LinearGradientBrush StartPoint="0,1" EndPoint="0,0"><GradientStop Color="#286E7A3A" Offset="0"/><GradientStop Color="#006E7A3A" Offset="1"/></LinearGradientBrush>
      </Rectangle.Fill>
    </Rectangle>

    <Grid Margin="22,14,22,14">
      <Grid.RowDefinitions>
        <RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/>
      </Grid.RowDefinitions>

      <!-- header -->
      <DockPanel>
        <Border DockPanel.Dock="Right" Background="#17190F" BorderBrush="#3A3D27" BorderThickness="1" Padding="12,8" VerticalAlignment="Center">
          <StackPanel>
            <TextBlock Text="BACKEND" Style="{StaticResource Lbl}"/>
            <WrapPanel>
              <StackPanel Orientation="Horizontal" Margin="0,0,14,6"><Ellipse x:Name="DotMysql" Width="9" Height="9" Margin="0,0,6,0"/><TextBlock Text="MYSQL " FontSize="12"/><TextBlock x:Name="TxtMysql" FontFamily="Consolas" FontSize="12"/></StackPanel>
              <StackPanel Orientation="Horizontal" Margin="0,0,14,6" x:Name="WebRow"><Ellipse x:Name="DotWeb" Width="9" Height="9" Margin="0,0,6,0"/><TextBlock Text="WEB " FontSize="12"/><TextBlock x:Name="TxtWeb" FontFamily="Consolas" FontSize="12"/></StackPanel>
              <StackPanel Orientation="Horizontal" Margin="0,0,14,6"><Ellipse x:Name="DotRedir" Width="9" Height="9" Margin="0,0,6,0"/><TextBlock Text="REDIRECTOR " FontSize="12"/><TextBlock x:Name="TxtRedir" FontFamily="Consolas" FontSize="12"/></StackPanel>
              <StackPanel Orientation="Horizontal" Margin="0,0,0,6"><Ellipse x:Name="DotEmu" Width="9" Height="9" Margin="0,0,6,0"/><TextBlock Text="BC2 EMU " FontSize="12"/><TextBlock x:Name="TxtEmu" FontFamily="Consolas" FontSize="12"/></StackPanel>
            </WrapPanel>
            <StackPanel Orientation="Horizontal">
              <Button x:Name="BtnBackStart" Style="{StaticResource ABtn}" Content="START BACKEND" Margin="0,0,8,0" Padding="10,4"/>
              <Button x:Name="BtnBackStop" Style="{StaticResource Btn}" Content="STOP" Margin="0" Padding="10,4"/>
            </StackPanel>
          </StackPanel>
        </Border>
        <StackPanel VerticalAlignment="Center">
          <StackPanel Orientation="Horizontal">
            <TextBlock Text="PROJECT " FontFamily="{StaticResource Stencil}" FontSize="46" Foreground="#ECE4CA"/>
            <TextBlock Text="PHOENIX" FontFamily="{StaticResource Stencil}" FontSize="46" Foreground="#E8A33D"/>
          </StackPanel>
          <TextBlock x:Name="Sub" Text="BATTLEFIELD BAD COMPANY 2  //  OFFLINE BOTS" FontFamily="Consolas" FontSize="13" Foreground="#968F75"/>
        </StackPanel>
      </DockPanel>
      <Rectangle Grid.Row="1" Height="6" Margin="0,12,0,12" Fill="{StaticResource Hazard}" Opacity="0.85"/>

      <!-- servers -->
      <Grid Grid.Row="2" Margin="0,0,0,12" Height="128">
        <Grid.ColumnDefinitions><ColumnDefinition/><ColumnDefinition/><ColumnDefinition/><ColumnDefinition/></Grid.ColumnDefinitions>
        @@CARDS@@
      </Grid>

      <Grid Grid.Row="3">
        <Grid.ColumnDefinitions><ColumnDefinition Width="340"/><ColumnDefinition Width="14"/><ColumnDefinition Width="*"/><ColumnDefinition Width="14"/><ColumnDefinition Width="330"/></Grid.ColumnDefinitions>

        <!-- selected server + bots -->
        <Border Background="#17190F" BorderBrush="#3A3D27" BorderThickness="1" Padding="14,12">
          <ScrollViewer VerticalScrollBarVisibility="Auto">
            <StackPanel>
              <TextBlock x:Name="SelTitle" Text="SERVER 1" Style="{StaticResource H}" FontSize="22" Margin="0"/>
              <TextBlock x:Name="SelName" FontSize="12" Foreground="#968F75" TextTrimming="CharacterEllipsis"/>
              <TextBlock x:Name="SelPorts" FontFamily="Consolas" FontSize="11" Foreground="#968F75" Margin="0,2,0,6"/>
              <TextBlock x:Name="SelStatus" FontSize="13" TextWrapping="Wrap" Margin="0,0,0,10"/>
              <WrapPanel>
                <Button x:Name="BtnStart" Style="{StaticResource ABtn}" Content="&#9654; START"/>
                <Button x:Name="BtnStop" Style="{StaticResource Btn}" Content="STOP"/>
                <Button x:Name="BtnRestart" Style="{StaticResource Btn}" Content="RESTART"/>
                <Button x:Name="BtnRound" Style="{StaticResource Btn}" Content="RESTART ROUND"/>
                <Button x:Name="BtnNext" Style="{StaticResource Btn}" Content="NEXT MAP"/>
              </WrapPanel>
              <TextBlock Text="BOTS ON THIS SERVER" Style="{StaticResource Lbl}" Margin="0,6,0,4"/>
              <WrapPanel>
                <Button x:Name="BtnBotsOn" Style="{StaticResource Btn}" Content="PHOENIX: ON"/>
                <Button x:Name="BtnAttach" Style="{StaticResource ABtn}" Content="ATTACH BOTS NOW"/>
                <Button x:Name="BtnDetach" Style="{StaticResource Btn}" Content="REMOVE BOTS"/>
              </WrapPanel>
              <TextBlock Text="BOT SETTINGS (ALL SERVERS - USED WHEN THE BOTS ATTACH)" Style="{StaticResource Lbl}" Margin="0,6,0,4" TextWrapping="Wrap"/>
              <StackPanel Orientation="Horizontal" Margin="0,0,0,8">
                <Button x:Name="BtnBotsDn" Style="{StaticResource Btn}" Content="-" Width="34" Margin="0"/>
                <TextBlock x:Name="TxtBots" Text="15" FontFamily="{StaticResource Stencil}" FontSize="24" Width="46" TextAlignment="Center" VerticalAlignment="Center" Foreground="#E8A33D"/>
                <Button x:Name="BtnBotsUp" Style="{StaticResource Btn}" Content="+" Width="34" Margin="0"/>
                <TextBlock Text="  bots per team" VerticalAlignment="Center" Foreground="#968F75"/>
              </StackPanel>
              <WrapPanel>
                <Button x:Name="BtnDiff" Style="{StaticResource Btn}" Content="DIFFICULTY"/>
                <Button x:Name="BtnVeh" Style="{StaticResource Btn}" Content="VEHICLES"/>
                <Button x:Name="BtnHeli" Style="{StaticResource Btn}" Content="HELICOPTERS"/>
                <Button x:Name="BtnRoutes" Style="{StaticResource Btn}" Content="ROUTES"/>
                <Button x:Name="BtnAutoRoutes" Style="{StaticResource Btn}" Content="AUTO ROUTES"/>
              </WrapPanel>
              <TextBlock Text="TOOLS" Style="{StaticResource Lbl}" Margin="0,6,0,4"/>
              <WrapPanel>
                <Button x:Name="Btn3D" Style="{StaticResource ABtn}" Content="3D MAP + ROUTE EDITOR"/>
                <Button x:Name="BtnScan" Style="{StaticResource ABtn}" Content="SCAN ALL MAPS"/>
                <Button x:Name="BtnLearn" Style="{StaticResource Btn}" Content="LEARN VIEWER"/>
                <Button x:Name="BtnLog" Style="{StaticResource Btn}" Content="BOT LOG"/>
                <Button x:Name="BtnNav" Style="{StaticResource Btn}" Content="NAVCACHE"/>
                <Button x:Name="BtnIni" Style="{StaticResource Btn}" Content="PHOENIX.INI"/>
                <Button x:Name="BtnClassic" Style="{StaticResource Btn}" Content="CLASSIC LAUNCHER"/>
                <Button x:Name="BtnPaths" Style="{StaticResource Btn}" Content="PATHS"/>
                <Button x:Name="BtnPics" Style="{StaticResource Btn}" Content="MAP PICTURES"/>
              </WrapPanel>
            </StackPanel>
          </ScrollViewer>
        </Border>

        <!-- the selected server's map list -->
        <Border Grid.Column="2" Background="#17190F" BorderBrush="#3A3D27" BorderThickness="1" Padding="14,12">
          <DockPanel>
            <DockPanel DockPanel.Dock="Top">
              <TextBlock DockPanel.Dock="Right" x:Name="RotCount" FontFamily="Consolas" FontSize="12" Foreground="#968F75" VerticalAlignment="Center"/>
              <TextBlock x:Name="RotTitle" Text="MAP LIST" Style="{StaticResource H}"/>
            </DockPanel>
            <TextBlock DockPanel.Dock="Top" x:Name="RotMode" FontFamily="Consolas" FontSize="12" Foreground="#9CAF55" Margin="0,0,0,6"/>
            <StackPanel DockPanel.Dock="Bottom" Margin="0,10,0,0">
              <WrapPanel>
                <Button x:Name="BtnPlay" Style="{StaticResource ABtn}" Content="&#9654; PLAY THIS MAP NEXT"/>
                <Button x:Name="BtnUp" Style="{StaticResource Btn}" Content="&#9650;" Width="38"/>
                <Button x:Name="BtnDown" Style="{StaticResource Btn}" Content="&#9660;" Width="38"/>
                <Button x:Name="BtnRemove" Style="{StaticResource Btn}" Content="REMOVE"/>
                <Button x:Name="BtnRounds" Style="{StaticResource Btn}" Content="ROUNDS: 1"/>
              </WrapPanel>
              <TextBlock FontSize="11" Foreground="#968F75" TextWrapping="Wrap" Text="Bad Company 2 plays one mode per map list. A running server reads the list again at once; the server always starts on the first map."/>
            </StackPanel>
            <ListBox x:Name="LstRot" FontSize="13"/>
          </DockPanel>
        </Border>

        <!-- map catalogue -->
        <Border Grid.Column="4" Background="#17190F" BorderBrush="#3A3D27" BorderThickness="1" Padding="14,12">
          <DockPanel>
            <TextBlock DockPanel.Dock="Top" Text="MAPS" Style="{StaticResource H}"/>
            <Border DockPanel.Dock="Top" Height="168" Margin="0,0,0,8" Background="#11120C" BorderBrush="#3A3D27" BorderThickness="1" ClipToBounds="True">
              <Grid>
                <Image x:Name="MapPreview" Stretch="UniformToFill" VerticalAlignment="Center"/>
                <TextBlock x:Name="PreviewNone" Text="" Foreground="#968F75" FontSize="12" TextWrapping="Wrap" Margin="12" VerticalAlignment="Center" TextAlignment="Center"/>
                <Border VerticalAlignment="Bottom" Background="#CC0D0E09" Padding="8,4">
                  <StackPanel>
                    <TextBlock x:Name="PreviewName" Text="" FontFamily="{StaticResource Stencil}" FontSize="17" Foreground="#E8A33D"/>
                    <TextBlock x:Name="PreviewSub" Text="" FontFamily="Consolas" FontSize="11" Foreground="#C9C1A6"/>
                  </StackPanel>
                </Border>
              </Grid>
            </Border>
            <ListBox DockPanel.Dock="Top" x:Name="LstModes" Background="Transparent" BorderThickness="0" ItemContainerStyle="{StaticResource Chip}">
              <ListBox.ItemsPanel><ItemsPanelTemplate><WrapPanel/></ItemsPanelTemplate></ListBox.ItemsPanel>
            </ListBox>
            <CheckBox DockPanel.Dock="Top" x:Name="ChkViet" Content="Vietnam maps" Foreground="#ECE4CA" Margin="0,2,0,8"/>
            <StackPanel DockPanel.Dock="Bottom" Margin="0,10,0,0">
              <WrapPanel>
                <Button x:Name="BtnAdd" Style="{StaticResource ABtn}" Content="ADD TO LIST"/>
                <Button x:Name="BtnReplace" Style="{StaticResource Btn}" Content="REPLACE LIST"/>
                <Button x:Name="BtnAllMode" Style="{StaticResource Btn}" Content="SWITCH LIST TO MODE"/>
              </WrapPanel>
              <TextBlock FontSize="11" Foreground="#968F75" TextWrapping="Wrap" Text="Ctrl / Shift picks several. Bots play Conquest, Rush and Squad Rush objectives; in Squad Deathmatch they fight."/>
            </StackPanel>
            <ListBox x:Name="LstMaps" SelectionMode="Extended" FontSize="13"/>
          </DockPanel>
        </Border>
      </Grid>

      <!-- log + play -->
      <DockPanel Grid.Row="4" Margin="0,12,0,0">
        <Button DockPanel.Dock="Right" x:Name="BtnGame" Style="{StaticResource ABtn}" Content="&#9654; PLAY BAD COMPANY 2" FontFamily="{StaticResource Stencil}" FontSize="20" Padding="22,10" Margin="12,0,0,0"/>
        <Border Background="#11120C" BorderBrush="#3A3D27" BorderThickness="1" Padding="10,6">
          <TextBlock x:Name="Log" FontFamily="Consolas" FontSize="12" Foreground="#C9C1A6" TextWrapping="Wrap" MinHeight="48"/>
        </Border>
      </DockPanel>
    </Grid>
  </Grid>
</Window>
'@
$xamlText = $xamlText.Replace('@@CARDS@@', $cardXaml)

try { $win = [Windows.Markup.XamlReader]::Parse($xamlText) } catch { Write-Host "Phoenix Launcher: the window could not be built: $($_.Exception.Message)"; exit 1 }
$ui = @{}
foreach ($nm in 'DotMysql', 'TxtMysql', 'DotWeb', 'TxtWeb', 'WebRow', 'DotRedir', 'TxtRedir', 'DotEmu', 'TxtEmu', 'BtnBackStart', 'BtnBackStop', 'Sub',
    'SelTitle', 'SelName', 'SelPorts', 'SelStatus', 'BtnStart', 'BtnStop', 'BtnRestart', 'BtnRound', 'BtnNext', 'BtnBotsOn', 'BtnAttach', 'BtnDetach',
    'BtnBotsDn', 'BtnBotsUp', 'TxtBots', 'BtnDiff', 'BtnVeh', 'BtnHeli', 'BtnRoutes', 'BtnAutoRoutes', 'Btn3D', 'BtnScan', 'BtnLearn', 'BtnLog', 'BtnNav', 'BtnIni', 'BtnClassic', 'BtnPaths',
    'RotCount', 'RotTitle', 'RotMode', 'LstRot', 'BtnPlay', 'BtnUp', 'BtnDown', 'BtnRemove', 'BtnRounds', 'LstModes', 'ChkViet', 'LstMaps', 'BtnAdd', 'BtnReplace', 'BtnAllMode',
    'Log', 'BtnGame', 'MapPreview', 'PreviewNone', 'PreviewName', 'PreviewSub', 'BtnPics') { $ui[$nm] = $win.FindName($nm) }
foreach ($n in 1..4) { foreach ($p in 'Card', 'CardState', 'CardName', 'CardMap', 'CardPlayers', 'CardBots', 'CardPic', 'CardPicBox') { $ui["$p$n"] = $win.FindName("$p$n") } }
$ui.Sub.Text = "BATTLEFIELD BAD COMPANY 2  //  OFFLINE BOTS  //  v$Version"

function Brush([string]$hex) { $b = New-Object Windows.Media.SolidColorBrush ([Windows.Media.ColorConverter]::ConvertFromString($hex)); $b.Freeze(); $b }
$cAmber = Brush '#E8A33D'; $cOlive = Brush '#9CAF55'; $cDim = Brush '#968F75'; $cTxt = Brush '#ECE4CA'; $cEdge = Brush '#3A3D27'; $cOffDot = Brush '#3A2A12'; $cRed = Brush '#D0563A'

$logLines = New-Object Collections.Generic.List[string]
function Say([string]$t) {
    $logLines.Add("$(Get-Date -Format HH:mm:ss)  $t"); while ($logLines.Count -gt 3) { $logLines.RemoveAt(0) }
    $ui.Log.Text = ($logLines -join "`n")
    Pump
}
function Pump { try { $win.Dispatcher.Invoke([Action] {}, [Windows.Threading.DispatcherPriority]::Background) } catch {} }
function Wait-Ms([int]$ms) { $end = (Get-Date).AddMilliseconds($ms); while ((Get-Date) -lt $end) { Pump; Start-Sleep -Milliseconds 50 } }
$script:busy = $false
function Safe([scriptblock]$b) {
    if ($script:busy) { Say 'Busy - one moment...'; return }
    $script:busy = $true
    try { & $b } catch { Say "Error: $($_.Exception.Message)" } finally { $script:busy = $false }
}

# ---------------------------------------------------------------- backend
function Start-Svc([string]$key) {
    if (Svc-Running $key) { return $true }
    switch ($key) {
        'mysql' {
            # Never start mysqld hidden: with no console its log lines can end up in the privilege tables.
            $safe = Join-Path $Portable 'Start-MySQL-Safe.ps1'
            Say 'Starting MySQL...'
            if (Test-Path $safe) {
                $p = Start-Process -FilePath 'powershell.exe' -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $safe + '"') -WindowStyle Minimized -PassThru
                for ($i = 0; $i -lt 150; $i++) { Wait-Ms 400; if ((Port-Open 3306) -or $p.HasExited) { break } }
            } else {
                $bat = Join-Path $xampp 'mysql_start.bat'
                if (-not (Test-Path $bat)) { Say "MySQL not found: $bat"; return $false }
                Start-Process -FilePath $bat -WorkingDirectory $xampp -WindowStyle Minimized | Out-Null
            }
            for ($i = 0; $i -lt 60 -and -not (Port-Open 3306); $i++) { Wait-Ms 400 }
            if (Port-Open 3306) { return $true }
            Say 'MySQL did not come up (see xampp\mysql\data\mysql_error.log).'; return $false
        }
        'web' {
            $exe = Join-Path $xampp 'apache\bin\httpd.exe'
            if (-not (Test-Path $exe)) { return $true }   # (BF-Portable's BC2 setup works without it)
            Say 'Starting the web server...'
            Start-Process -FilePath $exe -WorkingDirectory $xampp -WindowStyle Hidden | Out-Null
            Wait-Ms 1200; return (Svc-Running 'web')
        }
        'redir' {
            $exe = Join-Path $redirDir 'gosredirector.ea.com.exe'
            if (-not (Test-Path $exe)) { Say "Redirector not found: $exe"; return $false }
            Say 'Starting the redirector...'
            try { Start-Process -FilePath $exe -ArgumentList '/console' -WorkingDirectory $redirDir -WindowStyle Minimized | Out-Null }
            catch { Start-Process -FilePath $exe -ArgumentList '/console' -WorkingDirectory $redirDir -WindowStyle Minimized -Verb RunAs | Out-Null }
            Wait-Ms 1200; return (Svc-Running 'redir')
        }
        'emu' {
            $exe = Join-Path $emuDir 'mase_bc2.exe'
            if (-not (Test-Path $exe)) { Say "BC2 emulator not found: $exe"; return $false }
            Say 'Starting the BC2 emulator...'
            Start-Process -FilePath $exe -WorkingDirectory $emuDir -WindowStyle Minimized | Out-Null
            Wait-Ms 2500; return (Svc-Running 'emu')
        }
    }
    return $false
}
function Start-Backend {
    foreach ($k in 'mysql', 'web', 'redir', 'emu') {
        if (-not (Start-Svc $k)) { Say "$($services[$k].Name) did not start - the rest of the backend was not started."; Update-Services; return $false }
        Update-Services
    }
    return $true
}
function Stop-Backend {
    if (Get-ServerProcs) { Say 'Stop the BC2 servers first (they need the backend).'; return }
    foreach ($k in 'emu', 'redir', 'web') { Get-Process $services[$k].Proc -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue }
    # MySQL gets a clean shutdown: killing it leaves its tables half-written
    $stop = Join-Path $Portable 'Stop-MySQL-Safe.bat'; $adm = Join-Path $xampp 'mysql\bin\mysqladmin.exe'
    if (Svc-Running 'mysql') {
        if (Test-Path $stop) { Start-Process -FilePath $stop -WorkingDirectory $Portable -WindowStyle Minimized | Out-Null }
        elseif (Test-Path $adm) { & $adm -u root shutdown 2>$null }
        for ($i = 0; $i -lt 60 -and (Svc-Running 'mysql'); $i++) { Wait-Ms 250 }
    }
    Wait-Ms 600; Update-Services; Say 'Backend stopped.'
}
function Update-Services {
    $map = @{ mysql = @('DotMysql', 'TxtMysql'); web = @('DotWeb', 'TxtWeb'); redir = @('DotRedir', 'TxtRedir'); emu = @('DotEmu', 'TxtEmu') }
    foreach ($k in $map.Keys) {
        $on = Svc-Running $k
        $ui[$map[$k][0]].Fill = $(if ($on) { $cOlive } else { $cOffDot })
        $ui[$map[$k][1]].Text = $(if ($on) { 'ONLINE' } else { 'OFF' })
        $ui[$map[$k][1]].Foreground = $(if ($on) { $cOlive } else { $cDim })
    }
    $ui.WebRow.Visibility = $(if (Has-Web) { 'Visible' } else { 'Collapsed' })
}

# ---------------------------------------------------------------- map pictures (1.7.2)
# Each map's overhead picture is its minimap texture, read out of YOUR Bad Company 2 install (tools\thumbs\PhoenixThumbs.cs,
# compiled here on first use) and saved as thumbs\<MAP>.png. Nothing is downloaded or shipped: no game install, no pictures.
$thumbDir = Join-Path $PSScriptRoot 'thumbs'
$thumbCache = @{}
$script:thumbsTried = $false; $script:thumbsRunning = $false
function Thumb-Base([string]$code) {
    $s = (Level-Code $code).ToUpper()
    foreach ($t in '_SDM', '_CQ', '_GR', '_SR', 'SDM', 'CQ', 'GR', 'SR') { if ($s.EndsWith($t) -and $s.Length -gt $t.Length + 2) { $s = $s.Substring(0, $s.Length - $t.Length); break } }
    if ($s -match '^NAM_.*\dR$') { $s = $s.Substring(0, $s.Length - 1) }
    # (Cold War and Heavy Metal are the campaign's levels: their overviews are SP_02's and SP_005's)
    if ($s -eq 'MP_SP_002') { $s = 'SP_02' } elseif ($s -eq 'MP_SP_005') { $s = 'SP_005' }
    return $s
}
function Get-Thumb([string]$code) {
    if (-not $code) { return $null }
    $b = Thumb-Base $code
    if ($thumbCache.ContainsKey($b)) { return $thumbCache[$b] }
    $f = Join-Path $thumbDir "$b.png"; $img = $null
    if (-not (Test-Path $f)) { $f = Join-Path $thumbDir "$b.scan.png" }   # (drawn from Phoenix's scan when the game has none)
    if (Test-Path $f) {
        try {
            $img = New-Object Windows.Media.Imaging.BitmapImage
            $img.BeginInit(); $img.CacheOption = 'OnLoad'; $img.UriSource = New-Object Uri $f; $img.DecodePixelWidth = 256; $img.EndInit(); $img.Freeze()
        } catch { $img = $null }
    }
    if ($img) { $thumbCache[$b] = $img }   # (a missing one is looked for again after the pictures are made)
    return $img
}
function Game-Roots {
    if (-not $gameExe) { return @() }
    $g = Split-Path $gameExe
    return @((Join-Path $g 'Package\levels'), (Join-Path $g 'Package'), (Join-Path $g 'Dist\win32\levels'), (Join-Path $g 'Dist'))
}
function Load-ThumbsType {
    if ('PhoenixThumbs' -as [type]) { return $true }
    try { Add-Type -Path (Join-Path $PSScriptRoot 'tools\thumbs\PhoenixThumbs.cs') -ErrorAction Stop; return $true }
    catch { Say "Map pictures: the picture reader could not be built ($($_.Exception.Message))."; return $false }
}
# maps with no overview in the game files (or no game folder set): a picture from Phoenix's own scan, when it has one
function Make-ScanThumbs {
    $nav = Get-NavDir; if (-not (Test-Path $nav) -or -not (Load-ThumbsType)) { return 0 }
    if (-not (Test-Path $thumbDir)) { New-Item -ItemType Directory -Force $thumbDir | Out-Null }
    $made = 0; $keys = @{}
    foreach ($o in Get-ChildItem $nav -Filter '*.objectives' -ErrorAction SilentlyContinue) {
        $lv = (Get-Content $o.FullName -TotalCount 4 -ErrorAction SilentlyContinue | Where-Object { $_ -match '^level ' } | Select-Object -First 1) -replace '^level\s+', ''
        if ($lv) { $keys[(Thumb-Base $lv)] = [IO.Path]::GetFileNameWithoutExtension($o.Name) }
    }
    foreach ($b in $keys.Keys) {
        if ((Test-Path (Join-Path $thumbDir "$b.png")) -or (Test-Path (Join-Path $thumbDir "$b.scan.png"))) { continue }
        $np = Join-Path $nav "$($keys[$b]).nav"
        if (Test-Path $np) { try { if ([PhoenixThumbs]::NavThumb($np, (Join-Path $thumbDir "$b.scan.png"), 256)) { $made++ } } catch {} }
    }
    return $made
}
function Make-Thumbs([bool]$asked) {
    if ($script:thumbsRunning) { if ($asked) { Say 'Map pictures: still working...' }; return }
    if (-not $gameExe -or -not (Test-Path $gameExe)) {
        $m = Make-ScanThumbs; $thumbCache.Clear(); Fill-Catalogue; Fill-Rotation
        if ($asked) { Say "Map pictures come from your Bad Company 2 install - set BFBC2Game.exe with PATHS. ($m drawn from Phoenix's scans meanwhile.)" }
        return
    }
    if (-not (Load-ThumbsType)) { return }
    $script:thumbsTried = $true; $script:thumbsRunning = $true
    [PhoenixThumbs]::Start([string[]](Game-Roots), $thumbDir, 256)
    Say 'Map pictures: reading the map overviews from your Bad Company 2 files (a minute or so, once)...'
}
function Thumbs-Check {
    if (-not $script:thumbsRunning) { return }
    if ([PhoenixThumbs]::Busy) { $ui.PreviewNone.Text = "Map pictures: $([PhoenixThumbs]::Status)"; return }
    $script:thumbsRunning = $false; $thumbCache.Clear()
    $scan = Make-ScanThumbs
    $n = @(Get-ChildItem $thumbDir -Filter '*.png' -ErrorAction SilentlyContinue).Count
    Say "Map pictures: $n picture(s) ($([PhoenixThumbs]::Status)$(if ($scan) { "; $scan drawn from Phoenix's scans" }))."
    Fill-Catalogue; Fill-Rotation; Refresh-Servers; Show-Preview $script:previewCode
}
# a small list row: picture (or a tile with the map's initials) + name + level code
function Map-Row([string]$code, [string]$title, [string]$sub, [int]$px) {
    $sp = New-Object Windows.Controls.StackPanel; $sp.Orientation = 'Horizontal'
    $img = Get-Thumb $code
    $box = New-Object Windows.Controls.Border; $box.Width = $px; $box.Height = $px; $box.Margin = '0,1,10,1'; $box.BorderBrush = $cEdge; $box.BorderThickness = 1
    if ($img) { $i = New-Object Windows.Controls.Image; $i.Source = $img; $i.Stretch = 'UniformToFill'; $box.Child = $i }
    else {
        $box.Background = Brush '#2A2C1A'
        $t = New-Object Windows.Controls.TextBlock; $t.Text = (($title -split '\s+' | Where-Object { $_ } | ForEach-Object { $_[0] }) -join '').ToUpper(); $t.Foreground = $cOlive
        $t.FontSize = [Math]::Max(10, $px / 3); $t.HorizontalAlignment = 'Center'; $t.VerticalAlignment = 'Center'; $t.FontWeight = [Windows.FontWeights]::Bold; $box.Child = $t
    }
    [void]$sp.Children.Add($box)
    $tx = New-Object Windows.Controls.StackPanel; $tx.VerticalAlignment = 'Center'
    $a = New-Object Windows.Controls.TextBlock; $a.Text = $title; $a.FontSize = 14; [void]$tx.Children.Add($a)
    $b = New-Object Windows.Controls.TextBlock; $b.Text = $sub; $b.FontSize = 11; $b.FontFamily = 'Consolas'; $b.Foreground = $cDim; [void]$tx.Children.Add($b)
    [void]$sp.Children.Add($tx)
    return $sp
}
$script:previewCode = ''
function Show-Preview([string]$code) {
    $script:previewCode = $code
    if (-not $code) { $ui.MapPreview.Source = $null; $ui.PreviewName.Text = ''; $ui.PreviewSub.Text = ''; $ui.PreviewNone.Text = 'Pick a map to see it.'; return }
    $d = Map-Def $code
    $ui.PreviewName.Text = (Map-Name $code).ToUpper()
    $ui.PreviewSub.Text = "$(if ($d) { Mode-Name $d[2] } else { '' })$(if ($d -and $d[3]) { ' - Vietnam' })   [$code]"
    $img = Get-Thumb $code; $ui.MapPreview.Source = $img
    $ui.PreviewNone.Text = if ($img) { '' } elseif ($script:thumbsRunning) { 'Map pictures: reading the game files...' } elseif (-not $gameExe) { 'No picture: set BFBC2Game.exe with PATHS and click MAP PICTURES.' } else { 'No picture for this map in your game files.' }
}

# ---------------------------------------------------------------- map lists (BC2: "MODE" line, then "Levels/<map> <rounds>")
$sel = [int]$set.Selected; if ($sel -lt 1 -or $sel -gt 4) { $sel = 1 }
function Cur-Srv { $srv[$script:sel] }
function Read-List($s) {
    $r = @{ Mode = 'CONQUEST'; Rows = @() }
    if (-not (Test-Path $s.MapList)) { return $r }
    $first = $true
    foreach ($l in Get-Content $s.MapList) {
        $t = $l.Trim(); if (-not $t) { continue }
        $p = $t -split '\s+'
        if ($first -and $p.Count -eq 1 -and $p[0] -notmatch '/') { $r.Mode = $p[0].ToUpper(); $first = $false; continue }
        $first = $false
        $r.Rows += , @((Level-Code $p[0]), $(if ($p.Count -ge 2) { $p[1] } else { '1' }))
    }
    return $r
}
function Backup-Once($path) { $b = "$path.phoenix.bak"; if ((Test-Path $path) -and -not (Test-Path $b)) { Copy-Item $path $b -ErrorAction SilentlyContinue } }
function Write-List($s, $list) {
    if (-not (Test-Path $s.Inst)) { New-Item -ItemType Directory -Force $s.Inst | Out-Null }
    Backup-Once $s.MapList
    $lines = @($list.Mode) + @(foreach ($r in $list.Rows) { "Levels/$($r[0]) $($r[1])" })
    [IO.File]::WriteAllLines($s.MapList, [string[]]$lines)
    # Vietnam maps need the Vietnam mod on the server (GameModID in ServerOptions.ini)
    $viet = @($list.Rows | Where-Object { $_[0] -like 'NAM_*' }).Count -gt 0
    if (Test-Path $s.Options) {
        $o = Get-Content $s.Options; $want = $(if ($viet) { 'GameModID = VIETNAM' } else { 'GameModID = BC2' })
        $had = $false; $chg = $false
        for ($i = 0; $i -lt $o.Count; $i++) { if ($o[$i] -match '^\s*GameModID\s*=') { $had = $true; if ($o[$i].Trim() -ne $want) { $o[$i] = $want; $chg = $true } } }
        if (-not $had) { $o += $want; $chg = $true }
        if ($chg) { Backup-Once $s.Options; Set-Content $s.Options $o -Encoding ascii; if ($s.State -ne 'off') { Say "Server $($s.N): $(if ($viet) { 'Vietnam' } else { 'BC2' }) maps need a server RESTART (GameModID changed)." } }
    }
    if ($s.State -eq 'live') { Invoke-Rcon $s @(, @('mapList.load')) | Out-Null }
}

# ---------------------------------------------------------------- bots (PhoenixAttach)
$attachJobs = @{}     # server n -> @{ Proc; Pid; Since }
$pendingAttach = @{}  # server n -> deadline: attach once its level is up
function Bots-Attach([int]$n) {
    $s = $srv[$n]
    if (-not $s.Pid) { Say "Server $n is not running."; return }
    if (-not (Test-Path $attachExe)) { Say "Not found: $attachExe"; return }
    if ($attachJobs[$n]) { return }
    Say "Attaching Phoenix to server $n (process $($s.Pid))..."
    $p = $null
    try { $p = Start-Process -FilePath $attachExe -ArgumentList "--start --pid $($s.Pid)" -WorkingDirectory (Split-Path $attachExe) -WindowStyle Hidden -PassThru } catch {}
    $attachJobs[$n] = @{ Proc = $p; Pid = $s.Pid; Since = Get-Date; Elevated = $false }
    $s.Bots = 'attaching'
}
function Bots-Check {
    foreach ($n in @($attachJobs.Keys)) {
        $j = $attachJobs[$n]; $s = $srv[$n]
        if ($j.Proc -and -not $j.Proc.HasExited) {
            # PhoenixAttach shows the first 40 s of the bot log, then quits: by then the module started
            continue
        }
        $code = if ($j.Proc) { $j.Proc.ExitCode } else { 5 }
        $attachJobs.Remove($n)
        switch ($code) {
            0 { $s.Bots = 'on'; Say "Server ${n}: bots attached." }
            5 {
                if ($j.Elevated) { $s.Bots = 'off'; Say "Server ${n}: Windows did not allow the attach."; break }
                Say "Server $n runs as administrator - Windows will ask for permission for the bots..."
                $p = $null
                try { $p = Start-Process -FilePath $attachExe -ArgumentList "--start --pid $($j.Pid)" -WorkingDirectory (Split-Path $attachExe) -WindowStyle Hidden -Verb RunAs -PassThru } catch { Say 'Permission refused - bots not attached.' }
                if ($p) { $attachJobs[$n] = @{ Proc = $p; Pid = $j.Pid; Since = Get-Date; Elevated = $true } } else { $s.Bots = 'off' }
            }
            8 { $s.Bots = 'on'; Say "Server ${n}: Phoenix was already attached (or refused to start - see BOT LOG)." }
            31 { $s.Bots = 'off'; Say "Server $n exited while the bots attached - see BOT LOG." }
            32 { $s.Bots = 'on'; Say "Server ${n}: the bot log reports RESULT FAILED - see BOT LOG." }
            default { $s.Bots = 'off'; Say "Server ${n}: attach ended with code $code - see BOT LOG." }
        }
    }
    # servers waiting for their level: attach as soon as RCON answers
    foreach ($n in @($pendingAttach.Keys)) {
        $s = $srv[$n]
        if ($s.State -eq 'off' -and (Get-Date) -gt $pendingAttach[$n]) { $pendingAttach.Remove($n); continue }
        if ($s.State -eq 'live' -and $s.Pid) { $pendingAttach.Remove($n); Bots-Attach $n }
    }
}
function Bots-Detach([int]$n) {
    $s = $srv[$n]
    if (-not $s.Pid) { Say "Server $n is not running."; return }
    Say "Removing the bots from server $n..."
    $r = $null
    try { $r = Start-Process -FilePath $attachExe -ArgumentList "--stop --pid $($s.Pid)" -WorkingDirectory (Split-Path $attachExe) -WindowStyle Hidden -PassThru -Wait } catch {}
    if ($r -and $r.ExitCode -eq 5) { try { $r = Start-Process -FilePath $attachExe -ArgumentList "--stop --pid $($s.Pid)" -WorkingDirectory (Split-Path $attachExe) -WindowStyle Hidden -Verb RunAs -PassThru -Wait } catch {} }
    $s.Bots = 'off'
    Say "Server ${n}: bots removed. (Phoenix can attach again after a server restart.)"
}

# ---------------------------------------------------------------- servers
function Refresh-Servers {
    $procs = Get-ServerProcs
    foreach ($n in 1..4) {
        $s = $srv[$n]
        $old = $s.Pid
        $s.Pid = Find-ServerPid $s $procs
        if ($s.Pid -ne $old) { $s.Bots = 'off' }
        $s.Info = $null
        if ($s.Pid) {
            $r = Invoke-Rcon $s @(, @('serverInfo'))
            if ($r -and $r[0][0] -eq 'OK') { $s.State = 'live'; $s.Info = $r[0] } else { $s.State = 'starting' }
        } elseif ($pendingAttach[$n] -and (Get-Date) -lt $pendingAttach[$n]) { $s.State = 'starting' } else { $s.State = 'off' }
        $st = $ui["CardState$n"]; $mp = $ui["CardMap$n"]; $pl = $ui["CardPlayers$n"]; $bt = $ui["CardBots$n"]
        $ui["CardName$n"].Text = $s.Name
        switch ($s.State) {
            'live' {
                $st.Text = 'LIVE'; $st.Foreground = $cOlive
                $mp.Text = "$(Map-Name $s.Info[5])  /  $(Mode-Name $s.Info[4])"
                $pl.Text = "$($s.Info[2])/$($s.Info[3]) PLAYERS   ROUND $([int]$s.Info[6] + 1)/$($s.Info[7])"
            }
            'starting' { $st.Text = 'LOADING'; $st.Foreground = $cAmber; $mp.Text = 'level loading...'; $pl.Text = '' }
            default {
                $st.Text = 'OFFLINE'; $st.Foreground = $cDim
                $l = Read-List $s
                $mp.Text = $(if ($l.Rows.Count) { "first: $(Map-Name $l.Rows[0][0])  /  $(Mode-Name $l.Mode)" } else { 'no maps in the list' })
                $pl.Text = ''
            }
        }
        $picCode = if ($s.State -eq 'live' -and $s.Info) { Level-Code $s.Info[5] } else { $l0 = Read-List $s; if ($l0.Rows.Count) { $l0.Rows[0][0] } else { '' } }
        $pic = Get-Thumb $picCode
        $ui["CardPic$n"].Source = $pic; $ui["CardPicBox$n"].Visibility = $(if ($pic) { 'Visible' } else { 'Collapsed' })
        $bt.Text = switch ($s.Bots) { 'on' { 'PHOENIX BOTS ACTIVE' } 'attaching' { 'ATTACHING BOTS...' } default { if ($set["BotsOn$n"] -ne '1') { 'bots off' } elseif ($s.State -eq 'off') { 'bots attach on start' } else { 'no bots attached' } } }
        $bt.Foreground = $(if ($s.Bots -eq 'on') { $cAmber } else { $cDim })
        $ui["Card$n"].BorderBrush = $(if ($n -eq $script:sel) { $cAmber } elseif ($s.State -eq 'live') { $cOlive } else { $cEdge })
    }
    Update-Selected
}
function Update-Selected {
    $s = Cur-Srv; $n = $s.N
    $ui.SelTitle.Text = "SERVER $n"; $ui.SelName.Text = $s.Name
    $ui.SelPorts.Text = "GAME $($s.GamePort)   RCON $($s.RconPort)   $($s.Login)"
    $ui.RotTitle.Text = "MAP LIST // SERVER $n"
    switch ($s.State) {
        'live' { $ui.SelStatus.Text = "LIVE: $(Map-Name $s.Info[5]), $(Mode-Name $s.Info[4])  -  $($s.Info[2]) of $($s.Info[3]) players"; $ui.SelStatus.Foreground = $cOlive }
        'starting' { $ui.SelStatus.Text = 'LOADING (not answering RCON yet)...'; $ui.SelStatus.Foreground = $cAmber }
        default { $ui.SelStatus.Text = $(if (Test-Path $s.Bat) { 'OFFLINE' } else { "OFFLINE - $(Split-Path $s.Bat -Leaf) not found (PATHS)" }); $ui.SelStatus.Foreground = $cDim }
    }
    $on = $set["BotsOn$n"] -eq '1'
    $ui.BtnBotsOn.Content = $(if ($on) { 'AUTO-ATTACH: ON' } else { 'AUTO-ATTACH: OFF' })
    $ui.BtnStart.IsEnabled = $s.State -eq 'off'
    $ui.BtnStop.IsEnabled = $s.State -ne 'off'
    $ui.BtnRestart.IsEnabled = $s.State -ne 'off'
    $ui.BtnRound.IsEnabled = $s.State -eq 'live'
    $ui.BtnNext.IsEnabled = $s.State -eq 'live'
    $ui.BtnAttach.IsEnabled = ($s.State -eq 'live') -and ($s.Bots -eq 'off')
    $ui.BtnDetach.IsEnabled = ($s.Pid -ne 0) -and ($s.Bots -eq 'on')
    Update-BotSettings
}
function Update-BotSettings {
    $ui.TxtBots.Text = Ini-Get 'BotsPerTeam' '15'
    $d = [int](Ini-Get 'Difficulty' '2'); $ui.BtnDiff.Content = 'DIFFICULTY: ' + @('', 'EASY', 'NORMAL', 'HARD')[[Math]::Max(1, [Math]::Min(3, $d))]
    $v = [int](Ini-Get 'Vehicles' '2'); $ui.BtnVeh.Content = 'VEHICLES: ' + @('OFF', 'RIDE', 'DRIVE')[[Math]::Max(0, [Math]::Min(2, $v))]
    $ui.BtnHeli.Content = 'HELICOPTERS: ' + $(if ((Ini-Get 'Helicopters' '1') -ne '0') { 'ON' } else { 'OFF' })
    $ui.BtnRoutes.Content = 'ROUTES: ' + $(if ((Ini-Get 'Routes' '1') -ne '0') { 'ALWAYS' } else { 'OFF' })
    $ui.BtnAutoRoutes.Content = 'AUTO ROUTES: ' + $(if ((Ini-Get 'AutoRoutes' '1') -ne '0') { 'ON' } else { 'OFF' })
}
function Fill-Rotation([int]$keep = -1) {
    $s = Cur-Srv; $l = Read-List $s
    $curMap = ''; if ($s.State -eq 'live' -and $s.Info) { $curMap = Level-Code $s.Info[5] }
    $ui.LstRot.Items.Clear()
    for ($i = 0; $i -lt $l.Rows.Count; $i++) {
        $r = $l.Rows[$i]
        $tag = if ($curMap -and $r[0] -ieq $curMap) { '   - NOW' } elseif ($i -eq 0) { '   - FIRST' } else { '' }
        [void]$ui.LstRot.Items.Add((Map-Row $r[0] ("{0}. {1}{2}" -f ($i + 1), (Map-Name $r[0]), $tag) ("{0}   x{1} round(s)" -f $r[0], $r[1]) 40))
    }
    $ui.RotCount.Text = "$($l.Rows.Count) MAPS"
    $ui.RotMode.Text = "MODE: $((Mode-Name $l.Mode).ToUpper())$(if (@($l.Rows | Where-Object { $_[0] -like 'NAM_*' }).Count) { '   (VIETNAM)' })"
    if ($keep -ge 0 -and $keep -lt $ui.LstRot.Items.Count) { $ui.LstRot.SelectedIndex = $keep }
    $ui.BtnRounds.Content = "ROUNDS: $($set.Rounds)"
}
function Select-Server([int]$n) { $script:sel = $n; $set.Selected = "$n"; Save-Settings; Refresh-Servers; Fill-Rotation }

function Start-Bc2([int]$n) {
    $s = $srv[$n]
    Refresh-Servers
    if ($s.State -ne 'off') { Say "Server $n is already running."; return }
    if (-not (Test-Path $s.Bat)) { Say "Not found: $($s.Bat) - set the server folder with PATHS."; return }
    if (-not (Start-Backend)) { return }
    $l = Read-List $s
    Say "Starting server $n$(if ($l.Rows.Count) { " on $(Map-Name $l.Rows[0][0]), $(Mode-Name $l.Mode)" })..."
    Start-Process -FilePath $s.Bat -WorkingDirectory $server -WindowStyle Minimized | Out-Null
    if ($set["BotsOn$n"] -eq '1') { $pendingAttach[$n] = (Get-Date).AddMinutes(4) }
    Wait-Ms 2500; Refresh-Servers; Fill-Rotation
}
function Stop-Bc2([int]$n) {
    $s = $srv[$n]
    Refresh-Servers
    $pendingAttach.Remove($n); $attachJobs.Remove($n)
    if ($s.State -eq 'off' -or -not $s.Pid) { Say "Server $n is not running."; return }
    Say "Stopping server $n..."
    # (BC2 has no RCON shutdown command: the process is ended. The bots go with it.)
    try { Stop-Process -Id $s.Pid -Force -ErrorAction Stop } catch { try { Start-Process -FilePath 'taskkill.exe' -ArgumentList "/PID $($s.Pid) /F" -Verb RunAs -WindowStyle Hidden -Wait } catch {} }
    $gone = (Get-Date).AddSeconds(10)
    while ((Get-Process -Id $s.Pid -ErrorAction SilentlyContinue) -and (Get-Date) -lt $gone) { Wait-Ms 300 }
    $s.Bots = 'off'
    Say "Server $n stopped."
    Refresh-Servers; Fill-Rotation
}
function Rcon-Do($s, [object[]]$cmds, [string]$what) {
    $r = Invoke-Rcon $s $cmds
    if (-not $r) { Say "Server $($s.N) is not answering."; return $false }
    $bad = @($r | Where-Object { $_[0] -ne 'OK' })
    if ($bad.Count) { Say "$what failed: $($bad[0] -join ' ')"; return $false }
    Say "$what."; return $true
}

# ---------------------------------------------------------------- tools
function Find-Node {
    $c = Get-Command node -ErrorAction SilentlyContinue; if ($c) { return $c.Source }
    foreach ($p in "$env:ProgramFiles\nodejs\node.exe", "${env:ProgramFiles(x86)}\nodejs\node.exe", (Join-Path $PSScriptRoot 'tools\node\node.exe')) { if ($p -and (Test-Path $p)) { return $p } }
    return $null
}
function Show-3D {
    $url = "http://localhost:$map3dPort/"
    if (Port-Open $map3dPort) {   # a 3D map server from an older version still running: replace it
        $v = $null; try { $v = (Invoke-RestMethod -Uri "$($url)api/maps" -TimeoutSec 3).version } catch {}
        if ($v -ne $Version) {
            try { Get-NetTCPConnection -LocalPort $map3dPort -State Listen -ErrorAction Stop | ForEach-Object { Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue } } catch {}
            for ($i = 0; $i -lt 20 -and (Port-Open $map3dPort); $i++) { Wait-Ms 200 }
        }
    }
    if (-not (Port-Open $map3dPort)) {
        $node = Find-Node
        if (-not $node) { Say 'The 3D map needs Node.js (nodejs.org, the LTS installer) - then click 3D MAP again.'; return }
        if (-not (Test-Path (Join-Path $map3dDir 'server.js'))) { Say "Not found: $map3dDir\server.js"; return }
        Say 'Starting the 3D map server...'
        Start-Process -FilePath $node -ArgumentList ('"' + (Join-Path $map3dDir 'server.js') + '"'), ('"' + (Get-NavDir) + '"'), $map3dPort -WorkingDirectory $map3dDir -WindowStyle Hidden | Out-Null
        for ($i = 0; $i -lt 30 -and -not (Port-Open $map3dPort); $i++) { Wait-Ms 200 }
    }
    Start-Process $url
    Say "3D map: $url  (scanned maps appear once the bots have mapped them; routes you save there are used within 5 s)."
}
function Latest-Report {
    $f = Join-Path $reportsDir 'latest.txt'
    if (Test-Path $f) { $d = (Get-Content $f -Raw).Trim(); if (Test-Path $d) { return $d } }
    $d = Get-ChildItem $reportsDir -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($d) { return $d.FullName }; return $null
}
function Pick-Folder([string]$what, [string]$start) {
    Add-Type -AssemblyName System.Windows.Forms
    $d = New-Object Windows.Forms.FolderBrowserDialog; $d.Description = $what; if ($start -and (Test-Path $start)) { $d.SelectedPath = $start }
    if ($d.ShowDialog() -eq 'OK') { return $d.SelectedPath }; return $null
}
function Pick-File([string]$what, [string]$filter, [string]$start) {
    $d = New-Object Microsoft.Win32.OpenFileDialog; $d.Title = $what; $d.Filter = $filter
    if ($start -and (Test-Path (Split-Path $start))) { $d.InitialDirectory = Split-Path $start }
    if ($d.ShowDialog()) { return $d.FileName }; return $null
}

# ---------------------------------------------------------------- wire up
function Sel-Mode { $i = $ui.LstModes.SelectedIndex; if ($i -lt 0) { $i = 0 }; $modes[$i] }
function Fill-Catalogue {
    $m = Sel-Mode; $viet = [bool]$ui.ChkViet.IsChecked
    $ui.LstMaps.Items.Clear(); $script:shown = @()
    foreach ($d in $mapDefs) {
        if ($d[2] -ne $m[0] -or [bool]$d[3] -ne $viet) { continue }
        [void]$ui.LstMaps.Items.Add((Map-Row $d[0] $d[1] $d[0] 48)); $script:shown += , $d
    }
}
foreach ($m in $modes) { [void]$ui.LstModes.Items.Add($(if ($m[2]) { "$([char]0x2605) $($m[1])" } else { $m[1] })) }
$ui.LstModes.SelectedIndex = 0
Fill-Catalogue
$ui.LstModes.Add_SelectionChanged({ Fill-Catalogue })
$ui.ChkViet.Add_Click({ Fill-Catalogue })
$ui.LstMaps.Add_SelectionChanged({ $i = $ui.LstMaps.SelectedIndex; if ($i -ge 0 -and $i -lt $script:shown.Count) { Show-Preview $script:shown[$i][0] } })
$ui.LstRot.Add_SelectionChanged({ $i = $ui.LstRot.SelectedIndex; if ($i -ge 0) { $l = Read-List (Cur-Srv); if ($i -lt $l.Rows.Count) { Show-Preview $l.Rows[$i][0] } } })
$ui.BtnPics.Add_Click({ Safe { $thumbCache.Clear(); Make-Thumbs $true } })

foreach ($n in 1..4) { $ui["Card$n"].Add_Click({ $t = [int]$this.Tag; Safe { Select-Server $t } }) }
$ui.BtnBackStart.Add_Click({ Safe { if (Start-Backend) { Say 'Backend running: MySQL, redirector, BC2 emulator.' } } })
$ui.BtnBackStop.Add_Click({ Safe { Stop-Backend } })
$ui.BtnStart.Add_Click({ Safe { Start-Bc2 $script:sel } })
$ui.BtnStop.Add_Click({ Safe { Stop-Bc2 $script:sel } })
$ui.BtnRestart.Add_Click({ Safe { $n = $script:sel; Stop-Bc2 $n; Wait-Ms 2000; Start-Bc2 $n } })
$ui.BtnRound.Add_Click({ Safe { Rcon-Do (Cur-Srv) @(, @('admin.restartMap')) 'Restarting the round' | Out-Null } })
$ui.BtnNext.Add_Click({ Safe { Rcon-Do (Cur-Srv) @(, @('admin.runNextLevel')) 'Moving to the next map' | Out-Null } })
$ui.BtnBotsOn.Add_Click({ Safe { $n = $script:sel; $set["BotsOn$n"] = $(if ($set["BotsOn$n"] -eq '1') { '0' } else { '1' }); Save-Settings; Refresh-Servers; Say "Server ${n}: bots $(if ($set["BotsOn$n"] -eq '1') { 'attach by themselves when it starts' } else { 'are not attached when it starts' })." } })
$ui.BtnAttach.Add_Click({ Safe { Bots-Attach $script:sel; Refresh-Servers } })
$ui.BtnDetach.Add_Click({ Safe { Bots-Detach $script:sel; Refresh-Servers } })

function Bot-Note { if (@($srv.Values | Where-Object { $_.Bots -eq 'on' }).Count) { ' (servers with bots already attached use it after a restart)' } else { '' } }
$ui.BtnBotsDn.Add_Click({ Safe { $v = [Math]::Max(0, [int](Ini-Get 'BotsPerTeam' '15') - 1); Ini-Set 'BotsPerTeam' "$v" 'Bots'; Update-BotSettings } })
$ui.BtnBotsUp.Add_Click({ Safe { $v = [Math]::Min(24, [int](Ini-Get 'BotsPerTeam' '15') + 1); Ini-Set 'BotsPerTeam' "$v" 'Bots'; Update-BotSettings } })
$ui.BtnBotsDn.Add_MouseRightButtonUp({ Safe { Ini-Set 'BotsPerTeam' '0' 'Bots'; Update-BotSettings } })
$ui.BtnBotsUp.Add_MouseRightButtonUp({ Safe { Ini-Set 'BotsPerTeam' '24' 'Bots'; Update-BotSettings } })
$ui.BtnDiff.Add_Click({ Safe { $d = [int](Ini-Get 'Difficulty' '2') % 3 + 1; Ini-Set 'Difficulty' "$d" 'Bots'; Update-BotSettings; Say "Bot difficulty: $(@('', 'easy', 'normal', 'hard')[$d])$(Bot-Note)." } })
$ui.BtnVeh.Add_Click({ Safe { $v = ([int](Ini-Get 'Vehicles' '2') + 1) % 3; Ini-Set 'Vehicles' "$v"; Update-BotSettings; Say "Vehicles: $(@('bots stay on foot', 'bots ride as passengers', 'bots drive and ride')[$v])$(Bot-Note)." } })
$ui.BtnHeli.Add_Click({ Safe { $v = $(if ((Ini-Get 'Helicopters' '1') -ne '0') { '0' } else { '1' }); Ini-Set 'Helicopters' $v; Update-BotSettings; Say "Helicopters $(if ($v -eq '1') { 'on' } else { 'off' })$(Bot-Note)." } })
$ui.BtnRoutes.Add_Click({ Safe { $v = $(if ((Ini-Get 'Routes' '1') -ne '0') { '0' } else { '1' }); Ini-Set 'Routes' $v 'Routes'; Update-BotSettings; Say $(if ($v -eq '1') { "Bots always walk and drive the saved routes$(Bot-Note)." } else { "Saved routes off: bots plan every move on the grid$(Bot-Note)." }) } })
$ui.BtnAutoRoutes.Add_Click({ Safe { $v = $(if ((Ini-Get 'AutoRoutes' '1') -ne '0') { '0' } else { '1' }); Ini-Set 'AutoRoutes' $v 'Routes'; Update-BotSettings; Say $(if ($v -eq '1') { "Bots plan the missing spawn-to-objective routes themselves$(Bot-Note)." } else { "Only routes you draw in the 3D map are used$(Bot-Note)." }) } })

$ui.Btn3D.Add_Click({ Safe { Show-3D } })
$ui.BtnLearn.Add_Click({ Safe { $p = Join-Path $PSScriptRoot 'viewer\LearnViewer.html'; if (Test-Path $p) { Start-Process $p; Say "LearnViewer: open the files in $(Get-NavDir)." } else { Say "Not found: $p" } } })
$ui.BtnLog.Add_Click({ Safe { $d = Latest-Report; if ($d -and (Test-Path (Join-Path $d 'runtime.log'))) { Start-Process notepad.exe ('"' + (Join-Path $d 'runtime.log') + '"') } elseif ($d) { Start-Process explorer.exe ('"' + $d + '"') } else { Say 'No bot log yet - it is made when the bots attach.' } } })
$ui.BtnNav.Add_Click({ Safe { $d = Get-NavDir; if (-not (Test-Path $d)) { New-Item -ItemType Directory -Force $d | Out-Null }; Start-Process explorer.exe ('"' + $d + '"'); Say "navcache: $d (shared by every Phoenix install)" } })
$ui.BtnIni.Add_Click({ Safe { if (Test-Path $iniPath) { Start-Process notepad.exe ('"' + $iniPath + '"') } } })
$ui.BtnClassic.Add_Click({ Safe { $p = Join-Path $PSScriptRoot 'PhoenixLauncher.exe'; if (-not (Test-Path $p)) { $p = Join-Path $PSScriptRoot 'bin\PhoenixLauncher.exe' }; if (Test-Path $p) { Start-Process -FilePath $p -WorkingDirectory $PSScriptRoot } } })
$ui.BtnPaths.Add_Click({ Safe {
    $f = Pick-Folder 'The BC2 server folder (the one with BFBC2.bat and Frost.Game.Main_Win32_Final.exe)' $server
    if ($f) { $script:server = $f; $set.ServerDir = $f; if ((Split-Path $f -Leaf) -ieq 'Battlefield_bc2_Server') { $script:Portable = Split-Path $f; $set.Portable = $script:Portable; $script:xampp = Join-Path $Portable 'xampp'; $script:redirDir = Join-Path $Portable 'Redirector'; $script:emuDir = Join-Path $Portable 'BFBC2-EMU' } }
    $g = Pick-File 'The game: BFBC2Game.exe' 'BFBC2Game.exe|BFBC2Game.exe|Programs|*.exe' $gameExe
    if ($g) { $script:gameExe = $g; $set.Game = $g }
    Save-Settings; Load-Servers; Refresh-Servers; Fill-Rotation; Update-Services
    Say "Server folder: $server   Game: $(if ($gameExe) { $gameExe } else { 'not set' })"
    if ($g) { $thumbCache.Clear(); Make-Thumbs $false }
} })
$ui.BtnGame.Add_Click({ Safe {
    if (-not $gameExe -or -not (Test-Path $gameExe)) {
        $g = Pick-File 'Where is BFBC2Game.exe?' 'BFBC2Game.exe|BFBC2Game.exe|Programs|*.exe' ''
        if (-not $g) { return }; $script:gameExe = $g; $set.Game = $g; Save-Settings
    }
    Say 'Starting Bad Company 2... (servers appear in its server browser once they are LIVE)'
    Start-Process -FilePath $gameExe -WorkingDirectory (Split-Path $gameExe) | Out-Null
} })

# map list editing (the selected server's maplist.txt)
$ui.BtnUp.Add_Click({ Safe {
    $i = $ui.LstRot.SelectedIndex; if ($i -lt 1) { return }
    $s = Cur-Srv; $l = Read-List $s; $t = $l.Rows[$i - 1]; $l.Rows[$i - 1] = $l.Rows[$i]; $l.Rows[$i] = $t; Write-List $s $l; Fill-Rotation ($i - 1)
} })
$ui.BtnDown.Add_Click({ Safe {
    $i = $ui.LstRot.SelectedIndex; $s = Cur-Srv; $l = Read-List $s; if ($i -lt 0 -or $i -ge $l.Rows.Count - 1) { return }
    $t = $l.Rows[$i + 1]; $l.Rows[$i + 1] = $l.Rows[$i]; $l.Rows[$i] = $t; Write-List $s $l; Fill-Rotation ($i + 1)
} })
$ui.BtnRemove.Add_Click({ Safe {
    $i = $ui.LstRot.SelectedIndex; $s = Cur-Srv; $l = Read-List $s; if ($i -lt 0 -or $i -ge $l.Rows.Count) { return }
    if ($l.Rows.Count -le 1) { Say 'A server needs at least one map.'; return }
    $new = @(); for ($k = 0; $k -lt $l.Rows.Count; $k++) { if ($k -ne $i) { $new += , $l.Rows[$k] } }
    $l.Rows = $new; Write-List $s $l; Fill-Rotation ([Math]::Min($i, $new.Count - 1)); Refresh-Servers
} })
$ui.BtnRounds.Add_Click({ Safe {
    $r = [int]$set.Rounds % 3 + 1; $set.Rounds = "$r"; Save-Settings
    $s = Cur-Srv; $l = Read-List $s; if ($l.Rows.Count) { foreach ($row in $l.Rows) { $row[1] = "$r" }; Write-List $s $l }
    Fill-Rotation; Say "Server ${script:sel}: $r round(s) per map."
} })
# PLAY THIS MAP NEXT: the list is rotated so the chosen map is first (nothing lost); a running server reloads it and moves on
$ui.BtnPlay.Add_Click({ Safe {
    $s = Cur-Srv; $i = $ui.LstRot.SelectedIndex; $l = Read-List $s; if ($i -lt 0 -or $i -ge $l.Rows.Count) { Say 'Pick a map in the list first.'; return }
    $r = $l.Rows[$i]
    $new = @($l.Rows[$i..($l.Rows.Count - 1)]); if ($i -gt 0) { $new += $l.Rows[0..($i - 1)] }
    $l.Rows = $new; $was = $s.State; $s.State = 'off'; Write-List $s $l; $s.State = $was
    if ($s.State -eq 'live') {
        if (Rcon-Do $s @(@('mapList.load'), @('mapList.nextLevelIndex', '0')) "Queued $(Map-Name $r[0])") {
            Rcon-Do $s @(, @('admin.runNextLevel')) "Switching server $($s.N) to $(Map-Name $r[0]) (loading takes a moment)" | Out-Null
        }
    } else { Say "Server $($s.N) will start on $(Map-Name $r[0])." }
    Fill-Rotation 0; Refresh-Servers
} })
function Picked-Maps { @($ui.LstMaps.SelectedItems | ForEach-Object { $script:shown[$ui.LstMaps.Items.IndexOf($_)] }) }
$ui.BtnAdd.Add_Click({ Safe {
    $picked = Picked-Maps
    if (-not $picked.Count) { Say 'Pick one or more maps on the right first (Ctrl / Shift for several).'; return }
    $m = Sel-Mode; $s = Cur-Srv; $l = Read-List $s
    if ($l.Rows.Count -and $l.Mode -ne $m[0]) { Say "This list plays $(Mode-Name $l.Mode) - BC2 has one mode per list. Use REPLACE LIST or SWITCH LIST TO MODE."; return }
    $vietList = @($l.Rows | Where-Object { $_[0] -like 'NAM_*' }).Count -gt 0
    if ($l.Rows.Count -and ($vietList -ne [bool]$ui.ChkViet.IsChecked)) { Say 'Vietnam and BC2 maps cannot share a list (the server runs one or the other). Use REPLACE LIST.'; return }
    $l.Mode = $m[0]; $added = 0
    foreach ($d in $picked) { if ($l.Rows | Where-Object { $_[0] -ieq $d[0] }) { continue }; $l.Rows += , @($d[0], $set.Rounds); $added++ }
    Write-List $s $l; Fill-Rotation; Refresh-Servers
    Say "Added $added map(s) to server $script:sel."
} })
$ui.BtnReplace.Add_Click({ Safe {
    $picked = Picked-Maps; $m = Sel-Mode
    if (-not $picked.Count) { $picked = $script:shown }
    $ok = [Windows.MessageBox]::Show("Replace server $($script:sel)'s map list with $($picked.Count) map(s) in $($m[1])?", 'Project Phoenix', 'YesNo')
    if ("$ok" -ne 'Yes') { return }
    $s = Cur-Srv; $l = @{ Mode = $m[0]; Rows = @(foreach ($d in $picked) { , @($d[0], $set.Rounds) }) }
    Write-List $s $l; Fill-Rotation; Refresh-Servers
    Say "Server ${script:sel}: $($picked.Count) map(s) in $($m[1])."
} })
# every map in the list -> the same map's level for the chosen mode (maps without that mode are dropped)
$ui.BtnAllMode.Add_Click({ Safe {
    $m = Sel-Mode; $s = Cur-Srv; $l = Read-List $s; if (-not $l.Rows.Count) { return }
    $new = @(); $dropped = 0
    foreach ($r in $l.Rows) {
        $d = Map-Def $r[0]; $hit = $null
        if ($d) { foreach ($x in $mapDefs) { if ($x[1] -eq $d[1] -and $x[2] -eq $m[0] -and $x[3] -eq $d[3]) { $hit = $x; break } } }
        if ($hit) { if (-not ($new | Where-Object { $_[0] -eq $hit[0] })) { $new += , @($hit[0], $r[1]) } } else { $dropped++ }
    }
    if (-not $new.Count) { Say "None of these maps has a $($m[1]) version."; return }
    $l.Mode = $m[0]; $l.Rows = $new; Write-List $s $l; Fill-Rotation; Refresh-Servers
    Say "Server ${script:sel}: list now plays $($m[1])$(if ($dropped) { " ($dropped map(s) without it left out)" })."
} })

# ---------------------------------------------------------------- map scanner (1.7.1)
# Plays every map once on the selected server with Phoenix in scan mode (ScanMode=1: no bots, the survey runs flat out) so
# the 3D map has every map: the scan (grid, stairs, vehicle scan), the flags or every Rush stage's M-COMs, the spawns and
# the vehicles. BC2 plays one mode per map list, so it runs one pass per mode (Vietnam last: it needs GameModID = VIETNAM);
# the server is restarted for each pass. The DLL writes navcache\scan.txt; when it says "done 1" for the map being played
# the launcher moves the server on (mapList.nextLevelIndex + admin.runNextLevel). The map list, ServerOptions.ini and
# phoenix.ini are put back at the end (or when the scan is stopped).
$script:scan = $null
function Read-KV([string]$path) {
    $h = @{}; if (Test-Path $path) { foreach ($l in Get-Content $path -ErrorAction SilentlyContinue) { if ($l -match '^(\S+)\s+(.*)$') { $h[$Matches[1]] = $Matches[2].Trim() } } }
    return $h
}
# the level codes already scanned: their grid, their objectives (and their vehicle scan when bots use vehicles) are on disk
function Get-ScannedCodes {
    $nav = Get-NavDir; $done = @{}
    if (-not (Test-Path $nav)) { return $done }
    $needVeh = (Ini-Get 'Vehicles' '2') -ne '0'
    foreach ($o in Get-ChildItem $nav -Filter '*.objectives' -ErrorAction SilentlyContinue) {
        $key = [IO.Path]::GetFileNameWithoutExtension($o.Name)
        if (-not (Test-Path (Join-Path $nav "$key.nav"))) { continue }
        if ($needVeh -and -not (Test-Path (Join-Path $nav "$key.dscan"))) { continue }
        $t = Get-Content $o.FullName -ErrorAction SilentlyContinue
        $lv = ($t | Where-Object { $_ -match '^level ' } | Select-Object -First 1) -replace '^level\s+', ''
        $code = Level-Code $lv
        if (-not $code) { continue }
        $hasObj = [bool]($t | Where-Object { $_ -match '^(flag|mcom) ' })
        $isDm = $code -match 'SDM$'
        if ($hasObj -or ($isDm -and ($t | Where-Object { $_ -match '^spawn ' }))) { $done[$code.ToUpper()] = $true }
    }
    return $done
}
function Scan-Plan([bool]$all) {
    $done = if ($all) { @{} } else { Get-ScannedCodes }
    $passes = @()
    foreach ($pass in @(@('CONQUEST', 0), @('RUSH', 0), @('SQRUSH', 0), @('SQDM', 0), @('CONQUEST', 1), @('RUSH', 1), @('SQRUSH', 1), @('SQDM', 1))) {
        $maps = @($mapDefs | Where-Object { $_[2] -eq $pass[0] -and $_[3] -eq $pass[1] -and -not $done[$_[0].ToUpper()] } | ForEach-Object { $_[0] })
        if ($maps.Count) { $passes += , @{ Mode = $pass[0]; Viet = [bool]$pass[1]; Maps = $maps } }
    }
    return , $passes
}
function Scan-Start {
    $s = Cur-Srv; $n = $s.N
    if (-not (Test-Path $s.Bat)) { Say "Server ${n}: $(Split-Path $s.Bat -Leaf) not found - click PATHS."; return }
    $todo = Scan-Plan $false; $every = Scan-Plan $true
    $cTodo = ($todo | ForEach-Object { $_.Maps.Count } | Measure-Object -Sum).Sum; $cAll = ($every | ForEach-Object { $_.Maps.Count } | Measure-Object -Sum).Sum
    $msg = "Scan maps on server $n?`n`nYES: the $cTodo map(s) not scanned yet`nNO: all $cAll maps again`n`n" +
           "Each map takes about 2 - 8 minutes (a new map longest). Server $n is restarted for each mode (Conquest, Rush, Squad Rush, Squad Deathmatch, then the same for Vietnam) and runs with no bots. " +
           "Its map list, ServerOptions.ini and phoenix.ini are put back afterwards. You can stop the scan at any time with STOP SCAN."
    $a = [Windows.MessageBox]::Show($msg, 'Project Phoenix - scan all maps', 'YesNoCancel', 'Question')
    if ("$a" -eq 'Cancel') { return }
    $plan = if ("$a" -eq 'No') { $every } else { $todo }
    if (-not $plan.Count) { Say 'Every map is scanned already. (Use NO in the question to scan them all again.)'; return }
    $script:scan = @{
        N = $n; Passes = $plan; Pass = -1; Idx = 0; Phase = 'pass'; MapSince = Get-Date; Ok = @(); Failed = @(); Total = ($plan | ForEach-Object { $_.Maps.Count } | Measure-Object -Sum).Sum
        List = $(if (Test-Path $s.MapList) { [IO.File]::ReadAllText($s.MapList) } else { $null })
        Options = $(if (Test-Path $s.Options) { [IO.File]::ReadAllText($s.Options) } else { $null })
        ScanIni = Ini-Get 'ScanMode' '0'; WasLive = ($s.State -ne 'off'); Restarts = 0
    }
    $ui.BtnScan.Content = 'STOP SCAN'
    Say "Map scan: $($script:scan.Total) map(s) in $($plan.Count) pass(es) on server $n."
    Scan-Step
}
function Scan-Status { $p = Join-Path (Get-NavDir) 'scan.txt'; Read-KV $p }
function Scan-PassStart {
    $sc = $script:scan; $s = $srv[$sc.N]; $pass = $sc.Passes[$sc.Pass]
    if ($s.State -ne 'off') { Stop-Bc2 $sc.N }
    $maps = @($pass.Maps[$sc.Idx..($pass.Maps.Count - 1)])
    $s.State = 'off'   # (so Write-List doesn't ask a server that is going away to reload)
    Write-List $s @{ Mode = $pass.Mode; Rows = @($maps | ForEach-Object { , @($_, '1') }) }
    Ini-Set 'ScanMode' '1' 'Scanner'
    Remove-Item (Join-Path (Get-NavDir) 'scan.txt') -ErrorAction SilentlyContinue
    Say "Map scan: $(Mode-Name $pass.Mode)$(if ($pass.Viet) { ' (Vietnam)' }) - $($maps.Count) map(s); starting server $($sc.N)..."
    $sc.ListStart = $sc.Idx
    Start-Bc2 $sc.N
    $pendingAttach[$sc.N] = (Get-Date).AddMinutes(6)
    $sc.Phase = 'wait'; $sc.MapSince = Get-Date
}
function Scan-Advance([string]$why) {
    $sc = $script:scan; $pass = $sc.Passes[$sc.Pass]; $code = $pass.Maps[$sc.Idx]
    if ($why -eq 'ok') { $sc.Ok += $code } else { $sc.Failed += "$code ($why)" }
    $sc.Idx++
    if ($sc.Idx -ge $pass.Maps.Count) { $sc.Phase = 'pass'; Scan-Step; return }
    $s = $srv[$sc.N]
    # the server's list starts at the map the pass (or its restart) started on
    $r = Invoke-Rcon $s @(@('mapList.nextLevelIndex', [string]($sc.Idx - $sc.ListStart)), @('admin.runNextLevel'))
    if (-not $r) { Say "Map scan: server $($sc.N) did not answer - restarting it on the next map."; $sc.Restarts++; Scan-PassStart; return }
    Remove-Item (Join-Path (Get-NavDir) 'scan.txt') -ErrorAction SilentlyContinue
    $sc.MapSince = Get-Date
}
function Scan-Finish([string]$why) {
    $sc = $script:scan; if (-not $sc) { return }
    $script:scan = $null
    $s = $srv[$sc.N]
    $pendingAttach.Remove($sc.N)
    if ($s.Pid) { Stop-Bc2 $sc.N }
    if ($null -ne $sc.List) { [IO.File]::WriteAllText($s.MapList, $sc.List) }
    if ($null -ne $sc.Options) { [IO.File]::WriteAllText($s.Options, $sc.Options) }
    Ini-Set 'ScanMode' $(if ($sc.ScanIni -eq '1') { '1' } else { '0' }) 'Scanner'
    $ui.BtnScan.Content = 'SCAN ALL MAPS'
    Say "Map scan $why`: $($sc.Ok.Count) map(s) scanned$(if ($sc.Failed.Count) { ", $($sc.Failed.Count) not: $($sc.Failed -join ', ')" }). Map list and settings put back - open the 3D MAP."
    Refresh-Servers; Fill-Rotation
}
function Scan-Step {
    $sc = $script:scan; if (-not $sc) { return }
    if ($sc.Phase -eq 'pass') {
        $sc.Pass++; $sc.Idx = 0
        if ($sc.Pass -ge $sc.Passes.Count) { Scan-Finish 'finished'; return }
        Scan-PassStart; return
    }
    $s = $srv[$sc.N]; $pass = $sc.Passes[$sc.Pass]; $code = $pass.Maps[$sc.Idx]
    $num = $sc.Ok.Count + $sc.Failed.Count + 1
    $mins = ((Get-Date) - $sc.MapSince).TotalMinutes
    if ($s.State -eq 'off' -and $mins -gt 1.5) {   # the server went away (crash, closed)
        if ($sc.Restarts -ge 6) { Scan-Finish 'stopped (the server kept stopping)'; return }
        Say "Map scan: server $($sc.N) stopped on $(Map-Name $code) - skipping that map and starting it again."
        $sc.Restarts++; $sc.Failed += "$code (server stopped)"; $sc.Idx++
        if ($sc.Idx -ge $pass.Maps.Count) { $sc.Phase = 'pass'; Scan-Step } else { Scan-PassStart }
        return
    }
    $st = Scan-Status
    $now = Level-Code $st['level']
    if ($now -and $now -ine $code) {
        # the server is on another map of this pass (it skipped one it could not load): catch up
        $j = [array]::IndexOf(@($pass.Maps | ForEach-Object { $_.ToUpper() }), $now.ToUpper())
        if ($j -gt $sc.Idx) { for ($q = $sc.Idx; $q -lt $j; $q++) { $sc.Failed += "$($pass.Maps[$q]) (the server skipped it)" }; $sc.Idx = $j; $code = $now; $sc.MapSince = Get-Date }
    }
    $where = "$num/$($sc.Total) $(Map-Name $code) - $(Mode-Name $pass.Mode)"
    if ($now -ieq $code) {
        if ($st['done'] -eq '1') { Say "Map scan $where`: done ($($st['objectives']) objective(s))."; Scan-Advance 'ok'; return }
        $what = if ([int]$st['nav'] -lt 100) { "grid $($st['nav'])%" } elseif ($st['stairs'] -ne '1') { 'stairs' } elseif ($st['vehicles'] -eq '0') { 'vehicle scan' } else { 'flags / M-COMs' }
        $ui.Log.Text = "MAP SCAN $where`: $what ($([int]$mins) min)"
    } else {
        $ui.Log.Text = "MAP SCAN $where`: $(if ($s.State -eq 'live') { 'waiting for Phoenix' } else { 'level loading' }) ($([int]$mins) min)"
    }
    if ($mins -gt 20) { Say "Map scan $where`: no result after 20 min - next map."; Scan-Advance 'timed out' }
}
$ui.BtnScan.Add_Click({ Safe { if ($script:scan) { Scan-Finish 'stopped' } else { Scan-Start } } })

# ---------------------------------------------------------------- go
$timer = New-Object Windows.Threading.DispatcherTimer
$timer.Interval = [TimeSpan]::FromSeconds(3)
$script:tick = 0
$timer.Add_Tick({
    if ($script:busy) { return }
    $script:busy = $true
    try {
        $script:tick++
        if ($attachJobs.Count -or $pendingAttach.Count -or ($script:tick % 2 -eq 0)) { Refresh-Servers }
        Bots-Check
        if ($script:scan) { Scan-Step }
        Thumbs-Check
        if ($script:tick % 4 -eq 0) { Update-Services }
    } catch {} finally { $script:busy = $false }
})
$win.Add_ContentRendered({
    Update-Services; Refresh-Servers; Fill-Rotation
    if (-not (Test-Path $server)) { Say "BC2 server folder not found ($server) - click PATHS." }
    elseif (-not (Test-Path $attachExe)) { Say 'bin\PhoenixAttach.exe is missing - unpack the whole Project Phoenix folder.' }
    else { Say "Ready. Server folder: $server" }
    Show-Preview ''
    # map pictures: made once from the game files (again with MAP PICTURES)
    if (-not @(Get-ChildItem $thumbDir -Filter '*.png' -ErrorAction SilentlyContinue | Where-Object { $_.Name -notlike '*.scan.png' }).Count) { Make-Thumbs $false }
    $timer.Start()
})
$win.Add_Closed({
    $timer.Stop(); Save-Settings
    if ($script:scan) {   # (closing mid-scan: the files go back; the server keeps the map it is on)
        $sc = $script:scan; $s = $srv[$sc.N]
        if ($null -ne $sc.List) { [IO.File]::WriteAllText($s.MapList, $sc.List) }
        if ($null -ne $sc.Options) { [IO.File]::WriteAllText($s.Options, $sc.Options) }
        Ini-Set 'ScanMode' $(if ($sc.ScanIni -eq '1') { '1' } else { '0' }) 'Scanner'
    }
})
[void]$win.ShowDialog()
