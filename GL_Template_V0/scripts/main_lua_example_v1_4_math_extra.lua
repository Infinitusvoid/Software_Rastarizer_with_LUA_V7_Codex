local M = {}

function M.smoothstep(edge0, edge1, x)
  local t = (x - edge0) / (edge1 - edge0)
  if t < 0 then t = 0 end
  if t > 1 then t = 1 end
  return t * t * (3 - 2 * t)
end

return M
