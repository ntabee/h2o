paths = io.lines("paths.txt")
request = function()
    p = paths()
    if (p == nil) then
      paths = io.lines("paths.txt")
      p = paths()
    end
    return wrk.format(nil, p)
end
