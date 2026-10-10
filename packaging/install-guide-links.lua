-- Keep download URLs visible in the plain-text install guide.
function Link(link)
    local content = link.content
    if pandoc.utils.stringify(content) ~= link.target then
        content:insert(pandoc.Space())
        content:insert(pandoc.Str("(" .. link.target .. ")"))
    end
    return content
end
