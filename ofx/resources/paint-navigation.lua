-- Resolve's native Color/Edit OFX timeline suite cannot seek. Use the bundled
-- Lua interpreter and documented Resolve API; no Python or utility installation.
local M={}
local function integer(n) return type(n)=='number' and n==math.floor(n) end
function M.parseTimecode(tc, fps)
  local h,m,s,sep,f=tc:match('^(%d+):(%d+):(%d+)([:;])(%d+)$')
  assert(h,'Invalid timeline timecode')
  h,m,s,f=tonumber(h),tonumber(m),tonumber(s),tonumber(f)
  assert(m<60 and s<60 and f<fps,'Invalid timeline timecode fields')
  local drop=sep==';' and (fps==30 and 2 or fps==60 and 4 or nil) or 0
  assert(drop,'Unsupported drop-frame rate')
  return ((h*60+m)*60+s)*fps+f-drop*(h*60+m-math.floor((h*60+m)/10)),sep
end
function M.timecode(frame,fps,sep)
  assert(integer(frame) and frame>=0 and integer(fps),'Invalid target frame')
  if sep==';' then
    local drop=fps==30 and 2 or fps==60 and 4 or nil
    assert(drop,'Unsupported drop-frame rate')
    local ten=fps*600-drop*9
    frame=frame%(ten*6*24)
    local d=math.floor(frame/ten);local m=frame%ten
    frame=frame+drop*9*d+(m>=drop and drop*math.floor((m-drop)/(fps*60-drop)) or 0)
  end
  local f=frame%fps;frame=math.floor(frame/fps)
  local s=frame%60;frame=math.floor(frame/60)
  local m=frame%60;local h=math.floor(frame/60)%24
  return string.format('%02d:%02d:%02d%s%02d',h,m,s,sep,f)
end
function M.seek(r,owner,current,target,first,last)
  assert(integer(current) and integer(target) and integer(first) and integer(last),'Noninteger effect frame')
  assert(current>=first and current<=last and target>=first and target<=last,'Effect frame outside source')
  assert(r,'Resolve scripting is unavailable; enable local scripting')
  local page=r:GetCurrentPage();assert(page=='color' or page=='edit','Stroke navigation requires Color or Edit')
  local p=r:GetProjectManager():GetCurrentProject();assert(p and not p:IsRenderingInProgress(),'No idle project')
  local t=p:GetCurrentTimeline();assert(t,'No timeline')
  local item=t:GetCurrentVideoItem();assert(item,'No current video clip')
  local fps=math.floor(tonumber(t:GetSetting('timelineFrameRate') or p:GetSetting('timelineFrameRate'))+.5)
  local now,sep=M.parseTimecode(t:GetCurrentTimecode(),fps)
  local start,finish,left,right=item:GetStart(),item:GetEnd(),item:GetLeftOffset(),item:GetRightOffset()
  local span=last-first+1;local visible=finish-start
  -- Hosts expose either the trimmed effect interval or the full source
  -- interval. Both mappings are independent of delayed viewer redraws.
  local origin
  if span==visible then origin=start-first
  elseif span==visible+left+right then origin=start-left-first
  else error('Effect range does not map to this native timeline clip') end
  local frame=origin+target
  assert(frame>=start and frame<finish,'Stroke is outside the current clip')
  assert(t:SetCurrentTimecode(M.timecode(frame,fps,sep)),'Resolve rejected the target timecode')
  local actual=M.parseTimecode(t:GetCurrentTimecode(),fps)
  assert(actual==frame,'Resolve playhead did not reach the requested frame')
  return frame
end
if arg and arg[1]=='--seek' then
  local ok,result=pcall(M.seek,bmd.scriptapp('Resolve'),arg[2],tonumber(arg[3]),tonumber(arg[4]),tonumber(arg[5]),tonumber(arg[6]))
  if ok then print('ADR_NAV OK '..result) else print('ADR_NAV ERROR '..tostring(result));os.exit(1) end
end
return M
