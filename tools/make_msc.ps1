# Создание файла консоли inf_editor.msc с оснасткой «Редактор INF».
# Оснастка должна быть зарегистрирована (regsvr32 inf_snapin.dll); запуск — от имени администратора.
# Файл сохранён в UTF-8 без BOM, поэтому выводимые строки только латиницей: сообщения
# для пользователя выводит install.cmd. Коды выхода: 0 — успех, 2 — неожиданная структура консоли.
param(
    [string]$Out = (Join-Path $PSScriptRoot '..\inf_editor.msc')
)

$ErrorActionPreference = 'Stop'
$clsid = '{2FCAD432-6FDA-4C23-9A31-14244910ED26}'
$Out = [System.IO.Path]::GetFullPath($Out)

$mmc = New-Object -ComObject MMC20.Application
try {
    $doc = $mmc.Document
    $doc.SnapIns.Add($clsid) | Out-Null

    # окно, корнем которого служит узел оснастки, а не «Корень консоли»
    $node = $doc.ScopeNamespace.GetChild($doc.RootNode)
    $doc.Views.Add($node) | Out-Null
    $doc.ActiveView.ActiveScopeNode = $node

    # пользовательский режим, ограниченный доступ, одно окно: нет команды «Новое окно отсюда»
    # и меню «Окно», консоль не предлагает сохранять себя при выходе
    $doc.Mode = 3
    $doc.SaveAs($Out)
}
finally {
    $mmc.Quit()
}

# скрытая MMC не закрывает исходное окно с «Корнем консоли», поэтому оно удаляется из файла:
# остаётся только окно, корнем которого служит узел оснастки
$utf8 = New-Object System.Text.UTF8Encoding($false)
$bytes = [System.IO.File]::ReadAllBytes($Out)
$hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
$skip = $(if ($hasBom) { 3 } else { 0 })
$text = $utf8.GetString($bytes, $skip, $bytes.Length - $skip)
$rootView = '(?s)[ \t]*<View ID="\d+"[^>]*>\s*<BookMark Name="RootNode" NodeID="1"/>.*?</View>\r?\n'
$fixed = [regex]::Replace($text, $rootView, '', 1)
if ($fixed -eq $text) {
    exit 2
}
$data = $utf8.GetBytes($fixed)
$stream = [System.IO.File]::Create($Out)
try {
    if ($hasBom) {
        $stream.Write([byte[]](0xEF, 0xBB, 0xBF), 0, 3)
    }
    $stream.Write($data, 0, $data.Length)
}
finally {
    $stream.Close()
}
exit 0
