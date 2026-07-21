# 清理历史
git filter-branch --force --index-filter "git rm --cached --ignore-unmatch tools/test_weather_python.py tools/verify_jwt.py tools/ed25519-public.pem tools/ed25519-private.pem" --prune-empty --tag-name-filter cat -- --all

# 清理缓存
git reflog expire --expire=now --all
git gc --prune=now --aggressive

# 强制推送
git push --force --all

# 添加 .gitignore
echo "tools/test_weather_python.py" >> .gitignore
echo "tools/verify_jwt.py" >> .gitignore
echo "tools/ed25519-public.pem" >> .gitignore
echo "tools/ed25519-private.pem" >> .gitignore
git add .gitignore
git commit -m "忽略敏感文件"
git push